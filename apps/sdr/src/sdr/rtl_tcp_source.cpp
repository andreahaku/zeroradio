/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "rtl_tcp_source.h"

// The whole implementation depends on fftw3f + POSIX sockets, enabled only on
// the desktop build (see CMake SDR_HAVE_RTLTCP). On other targets this is an
// empty translation unit and the screen falls back to the mock source.
#ifdef SDR_HAVE_RTLTCP

#include "audio_demod.h"

#include <fftw3.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace sdr {
namespace {

// FFT size: 8192 bins over a 2.4 MHz sample rate gives ~293 Hz resolution, so
// even the narrowest 0.1 MHz zoom span still maps >320 FFT bins onto the 320
// display columns (~1:1), while the widest 2.0 MHz span averages ~21 bins each.
constexpr int      kFftSize     = 8192;
constexpr uint32_t kSampleRate  = 2400000; // 2.4 Msps (full FFT bandwidth)
constexpr float    kDynRangeDb  = 50.0f;   // dB above the noise floor -> 1.0
constexpr float    kFloorEma    = 0.05f;   // noise-floor follow rate per frame

// rtl_tcp command opcodes (see librtlsdr rtl_tcp.c). Each command is one opcode
// byte followed by a big-endian uint32 parameter.
enum RtlCmd : uint8_t {
    kSetFrequency   = 0x01,
    kSetSampleRate  = 0x02,
    kSetGainMode    = 0x03, // 1 = manual, 0 = automatic (tuner AGC)
    kSetGain        = 0x04, // manual tuner gain, tenths of dB
    kSetFreqCorr    = 0x05,
    kSetAgcMode     = 0x08, // RTL2832 digital AGC
    kSetBiasTee     = 0x0e, // keep OFF: the v4 can feed 4.5 V to the antenna
};

// Effective RTL sample rate. Defaults to kSampleRate (2.4 Msps, full FFT
// bandwidth on a capable host). Override with SDR_SAMPLE_RATE to a lower rate
// so a weak host (the CardputerZero CM0) can drain the IQ stream in real time
// instead of letting the rtl_tcp server buffer grow (which adds tuning latency
// and waterfall jitter). Clamped to the RTL2832U's valid ranges.
uint32_t effective_sample_rate() {
    if (const char* env = std::getenv("SDR_SAMPLE_RATE"); env && env[0] != '\0') {
        const long v = std::atol(env);
        if ((v >= 900000 && v <= 3200000) || (v >= 225001 && v <= 300000)) {
            return static_cast<uint32_t>(v);
        }
    }
    return kSampleRate;
}

// RTL capture rate for a given visible span (page-2 zoom): match the span so we
// only stream what's shown, clamped to the RTL2832U minimum (300k-900k is an
// invalid gap) and the host's drain ceiling `cap`.
uint32_t rate_for_span(int32_t span_hz, uint32_t cap) {
    constexpr uint32_t kMinRate = 960000;
    uint32_t r = span_hz > 0 ? static_cast<uint32_t>(span_hz) : cap;
    if (r < kMinRate) r = kMinRate;
    if (r > cap)      r = cap;
    return r;
}

} // namespace

struct RtlTcpSource::Impl {
    std::string host;
    uint16_t    port;
    const uint32_t max_sample_rate{effective_sample_rate()}; // host drain ceiling
    std::atomic<uint32_t> sample_rate{max_sample_rate};       // active RTL rate

    std::atomic<int64_t> desired_center{0};
    std::atomic<int32_t> span_hz{static_cast<int32_t>(max_sample_rate)};
    std::atomic<bool>    desired_gain_auto{true};
    std::atomic<int>     desired_gain_tenth{297};
    std::atomic<bool>    running{true};

    // Latest FFT magnitudes in dB, fftshifted (index 0 = low edge, N-1 = high
    // edge), published by the reader thread and read by next_frame().
    std::mutex          frame_mutex;
    std::vector<float>  shared_db;     // kFftSize, guarded by frame_mutex
    bool                have_frame = false;

    // Consumer-side state (touched only on the UI thread, in next_frame()).
    std::vector<float>  scratch_db;    // kFftSize copy
    float               floor_ema = 0.0f;
    bool                floor_init = false;
    float               last_peak = 0.0f;

    // FFT plan + buffers (reader thread only).
    fftwf_complex* in  = nullptr;
    fftwf_complex* out = nullptr;
    fftwf_plan     plan = nullptr;
    std::vector<float> window;         // Hann, kFftSize

    // Audio demodulator, fed the continuous IQ alongside the FFT.
    AudioDemod                       audio;
    std::vector<std::complex<float>> audio_buf; // reused per recv chunk

    std::thread thread;

    explicit Impl(std::string h, uint16_t p, int64_t center)
        : host(std::move(h)), port(p), audio(static_cast<double>(effective_sample_rate())) {
        desired_center.store(center);
        shared_db.assign(kFftSize, -120.0f);
        scratch_db.assign(kFftSize, -120.0f);

        in  = fftwf_alloc_complex(kFftSize);
        out = fftwf_alloc_complex(kFftSize);
        plan = fftwf_plan_dft_1d(kFftSize, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

        window.resize(kFftSize);
        for (int n = 0; n < kFftSize; ++n) {
            window[n] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) *
                                                static_cast<float>(n) /
                                                static_cast<float>(kFftSize - 1)));
        }

        thread = std::thread([this] { run(); });
    }

    ~Impl() {
        running.store(false);
        if (thread.joinable()) {
            thread.join();
        }
        if (plan) fftwf_destroy_plan(plan);
        if (in)   fftwf_free(in);
        if (out)  fftwf_free(out);
    }

    // ---- reader thread ----

    static bool send_all(int fd, const uint8_t* buf, size_t len) {
        size_t off = 0;
        while (off < len) {
            const ssize_t n = ::send(fd, buf + off, len - off, MSG_NOSIGNAL);
            if (n <= 0) {
                return false;
            }
            off += static_cast<size_t>(n);
        }
        return true;
    }

    static bool send_cmd(int fd, uint8_t cmd, uint32_t param) {
        const uint8_t b[5] = {
            cmd,
            static_cast<uint8_t>((param >> 24) & 0xff),
            static_cast<uint8_t>((param >> 16) & 0xff),
            static_cast<uint8_t>((param >> 8) & 0xff),
            static_cast<uint8_t>(param & 0xff),
        };
        return send_all(fd, b, sizeof(b));
    }

    static bool apply_gain(int fd, bool automatic, int tenth) {
        if (automatic) {
            return send_cmd(fd, kSetGainMode, 0); // tuner AGC
        }
        return send_cmd(fd, kSetGainMode, 1) &&
               send_cmd(fd, kSetGain, static_cast<uint32_t>(tenth < 0 ? 0 : tenth));
    }

    int connect_once() {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            ::close(fd);
            return -1;
        }
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return -1;
        }

        // 250 ms read timeout so the loop can observe running == false.
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 250000;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        const int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        // rtl_tcp sends a 12-byte greeting: "RTL0" + tuner type + gain count.
        uint8_t greeting[12];
        if (!recv_exact(fd, greeting, sizeof(greeting)) ||
            std::memcmp(greeting, "RTL0", 4) != 0) {
            ::close(fd);
            return -1;
        }

        // Configure the receiver. Order matters little; do freq last.
        const int64_t center = desired_center.load();
#ifdef SDR_HAVE_AUDIO
        const uint32_t init_sr = max_sample_rate; // fixed rate keeps the audio demod in sync
#else
        const uint32_t init_sr = rate_for_span(span_hz.load(), max_sample_rate);
#endif
        sample_rate.store(init_sr);
        audio.set_input_rate(static_cast<double>(init_sr));
        send_cmd(fd, kSetSampleRate, init_sr);
        apply_gain(fd, desired_gain_auto.load(), desired_gain_tenth.load());
        send_cmd(fd, kSetAgcMode, 1);    // RTL2832 digital AGC on
        send_cmd(fd, kSetFreqCorr, 0);   // 0 ppm
        send_cmd(fd, kSetBiasTee, 0);    // safety: bias tee OFF
        send_cmd(fd, kSetFrequency,
                 static_cast<uint32_t>(center < 0 ? 0 : center));
        return fd;
    }

    bool recv_exact(int fd, uint8_t* buf, size_t len) {
        size_t off = 0;
        while (off < len) {
            const ssize_t n = ::recv(fd, buf + off, len - off, 0);
            if (n > 0) {
                off += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (!running.load()) return false;
                continue; // timeout, keep waiting
            }
            return false; // peer closed or hard error
        }
        return true;
    }

    void run() {
        std::vector<uint8_t> buf(1 << 16); // 64 KiB recv chunk
        int have = 0;                      // complex samples accumulated in `in`
        bool pending_i = false;
        uint8_t pending = 0;

        int fd = -1;
        int64_t current_center = desired_center.load();
        bool    current_gain_auto = desired_gain_auto.load();
        int     current_gain_tenth = desired_gain_tenth.load();
        uint32_t current_sr = sample_rate.load();

        while (running.load()) {
            if (fd < 0) {
                fd = connect_once();
                if (fd < 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
                current_center = desired_center.load();
                current_gain_auto = desired_gain_auto.load();
                current_gain_tenth = desired_gain_tenth.load();
                current_sr = sample_rate.load(); // connect_once set it from the span
                have = 0;
                pending_i = false;
            }

            // Retune the hardware if the VFO moved.
            const int64_t want = desired_center.load();
            if (want != current_center) {
                if (!send_cmd(fd, kSetFrequency,
                              static_cast<uint32_t>(want < 0 ? 0 : want))) {
                    ::close(fd);
                    fd = -1;
                    continue;
                }
                current_center = want;
                // Flush IQ still buffered at the OLD frequency so the waterfall
                // jumps to the new VFO at once instead of draining the backlog
                // (which on a slow host reads as "tuning lag"). Drop the
                // partially-accumulated frame too.
                while (::recv(fd, buf.data(), buf.size(), MSG_DONTWAIT) > 0) {
                    // discard stale pre-retune samples
                }
                have = 0;
                pending_i = false;
            }

            // Follow the page-2 zoom: capture only the visible span (clamped to
            // the RTL minimum and the host's drain ceiling). Changing the rate
            // invalidates buffered IQ, so flush + restart the frame like a retune,
            // and rebuild the audio demod for the new intermediate rate.
            const uint32_t want_sr = rate_for_span(span_hz.load(), max_sample_rate);
            if (want_sr != current_sr) {
                if (!send_cmd(fd, kSetSampleRate, want_sr)) {
                    ::close(fd);
                    fd = -1;
                    continue;
                }
                sample_rate.store(want_sr);
                current_sr = want_sr;
                audio.set_input_rate(static_cast<double>(want_sr));
                while (::recv(fd, buf.data(), buf.size(), MSG_DONTWAIT) > 0) {
                    // discard stale pre-rate-change samples
                }
                have = 0;
                pending_i = false;
            }

            // Apply a gain change.
            const bool g_auto = desired_gain_auto.load();
            const int  g_tenth = desired_gain_tenth.load();
            if (g_auto != current_gain_auto || g_tenth != current_gain_tenth) {
                if (!apply_gain(fd, g_auto, g_tenth)) {
                    ::close(fd);
                    fd = -1;
                    continue;
                }
                current_gain_auto = g_auto;
                current_gain_tenth = g_tenth;
            }

            const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
            if (n == 0) {
                ::close(fd);
                fd = -1;
                continue;
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue; // timeout, re-check running / retune
                }
                ::close(fd);
                fd = -1;
                continue;
            }

            // Unpack interleaved unsigned IQ into the FFT input buffer. A frame
            // is processed (and overwritten) every kFftSize samples; we consume
            // the socket as fast as it arrives, so no lag accumulates.
            audio_buf.clear();
            ssize_t idx = 0;
            while (idx < n) {
                if (!pending_i) {
                    pending = buf[idx++];
                    pending_i = true;
                    if (idx >= n) break;
                }
                const uint8_t q = buf[idx++];
                pending_i = false;

                const float fi = (static_cast<float>(pending) - 127.5f) * (1.0f / 127.5f);
                const float fq = (static_cast<float>(q) - 127.5f) * (1.0f / 127.5f);
                audio_buf.emplace_back(fi, fq);

                in[have][0] = fi;
                in[have][1] = fq;
                if (++have == kFftSize) {
                    process_frame();
                    have = 0;
                }
            }
            // Feed the continuous baseband to the audio demodulator.
            audio.process(audio_buf.data(), audio_buf.size());
        }

        if (fd >= 0) {
            ::close(fd);
        }
    }

    // Window + FFT + magnitude (dB, fftshifted), then publish.
    void process_frame() {
        for (int k = 0; k < kFftSize; ++k) {
            in[k][0] *= window[k];
            in[k][1] *= window[k];
        }
        fftwf_execute(plan);

        std::lock_guard<std::mutex> lock(frame_mutex);
        const int half = kFftSize / 2;
        for (int i = 0; i < kFftSize; ++i) {
            const int src = (i + half) & (kFftSize - 1); // power-of-two shift
            const float re = out[src][0];
            const float im = out[src][1];
            const float p = re * re + im * im;
            shared_db[i] = 10.0f * std::log10(p + 1e-12f);
        }
        have_frame = true;
    }
};

RtlTcpSource::RtlTcpSource(std::string host, uint16_t port, int64_t initial_center_hz)
    : impl_(std::make_unique<Impl>(std::move(host), port, initial_center_hz)) {}

RtlTcpSource::~RtlTcpSource() = default;

void RtlTcpSource::set_tuning(int64_t center_hz, int32_t span_hz) {
    impl_->desired_center.store(center_hz);
    impl_->span_hz.store(span_hz > 0 ? span_hz : static_cast<int32_t>(impl_->max_sample_rate));
}

void RtlTcpSource::set_mode(int mode) {
    impl_->audio.set_mode(mode);
}

void RtlTcpSource::set_volume(float vol) {
    impl_->audio.set_volume(vol);
}

void RtlTcpSource::set_muted(bool muted) {
    impl_->audio.set_muted(muted);
}

void RtlTcpSource::set_gain(bool automatic, int tenth_db) {
    impl_->desired_gain_auto.store(automatic);
    impl_->desired_gain_tenth.store(tenth_db);
}

float RtlTcpSource::last_peak() const {
    return impl_->last_peak;
}

void RtlTcpSource::next_frame(float* mags, int n_bins) {
    if (!mags || n_bins <= 0) {
        return;
    }

    bool have;
    {
        std::lock_guard<std::mutex> lock(impl_->frame_mutex);
        have = impl_->have_frame;
        if (have) {
            std::copy(impl_->shared_db.begin(), impl_->shared_db.end(),
                      impl_->scratch_db.begin());
        }
    }
    if (!have) {
        std::fill(mags, mags + n_bins, 0.0f); // not connected yet: blank
        impl_->last_peak = 0.0f;
        return;
    }

    // Central window covering the visible span out of the full sample rate.
    const float* db = impl_->scratch_db.data();
    double frac = static_cast<double>(impl_->span_hz.load()) /
                  static_cast<double>(impl_->sample_rate.load());
    if (frac > 1.0) frac = 1.0;
    if (frac < 1.0 / kFftSize) frac = 1.0 / kFftSize;
    const double visible = frac * kFftSize;
    const double start = (kFftSize - visible) * 0.5;

    // Resample to n_bins by averaging each column's source span, into mags as
    // raw dB first; normalize in a second pass once we know the floor.
    float frame_min = 1e30f;
    for (int j = 0; j < n_bins; ++j) {
        int lo = static_cast<int>(start + visible * j / n_bins);
        int hi = static_cast<int>(start + visible * (j + 1) / n_bins);
        if (hi <= lo) hi = lo + 1;
        if (lo < 0) lo = 0;
        if (hi > kFftSize) hi = kFftSize;
        float sum = 0.0f;
        for (int k = lo; k < hi; ++k) {
            sum += db[k];
        }
        const float avg = sum / static_cast<float>(hi - lo);
        mags[j] = avg; // raw dB for now
        if (avg < frame_min) frame_min = avg;
    }

    // Track a slowly-following noise floor (per-frame minimum) for auto-scaling.
    if (!impl_->floor_init) {
        impl_->floor_ema = frame_min;
        impl_->floor_init = true;
    } else {
        impl_->floor_ema += (frame_min - impl_->floor_ema) * kFloorEma;
    }

    const float floor = impl_->floor_ema;
    const float inv_range = 1.0f / kDynRangeDb;
    float peak = 0.0f;
    for (int j = 0; j < n_bins; ++j) {
        float v = (mags[j] - floor) * inv_range;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        mags[j] = v;
        if (v > peak) peak = v;
    }
    impl_->last_peak = peak;
}

} // namespace sdr

#else // !SDR_HAVE_RTLTCP

namespace sdr {

struct RtlTcpSource::Impl {}; // complete type so unique_ptr<Impl> can destruct

RtlTcpSource::RtlTcpSource(std::string, uint16_t, int64_t) {}
RtlTcpSource::~RtlTcpSource() = default;
void RtlTcpSource::next_frame(float* mags, int n_bins) {
    if (mags && n_bins > 0) {
        for (int i = 0; i < n_bins; ++i) mags[i] = 0.0f;
    }
}
float RtlTcpSource::last_peak() const { return 0.0f; }
void RtlTcpSource::set_tuning(int64_t, int32_t) {}
void RtlTcpSource::set_mode(int) {}
void RtlTcpSource::set_volume(float) {}
void RtlTcpSource::set_muted(bool) {}
void RtlTcpSource::set_gain(bool, int) {}

} // namespace sdr

#endif // SDR_HAVE_RTLTCP
