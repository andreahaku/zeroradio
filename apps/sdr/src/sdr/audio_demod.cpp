/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "audio_demod.h"

// The SDL2-backed audio player is built only when SDR_HAVE_AUDIO is set (desktop).
// On the device the rtl_tcp spectrum backend is built WITHOUT SDL2 (SDR_HAVE_RTLTCP
// without SDR_HAVE_AUDIO): the no-op stub below keeps RtlTcpSource compiling and
// running spectrum/waterfall only, with audio deferred to a future ALSA port.
#ifdef SDR_HAVE_AUDIO

#include <SDL2/SDL.h>

#include <atomic>
#include <cmath>
#include <vector>

namespace sdr {
namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr int    kAudioHz = 48000;

// Mode indices (match model::RadioMode order).
enum Mode { WFM = 0, NFM = 1, AM = 2, USB = 3, LSB = 4, CW = 5 };

// Windowed-sinc (Hann) low-pass, unity DC gain. fc_norm = cutoff / sample_rate.
std::vector<float> make_lowpass(int taps, double fc_norm) {
    std::vector<float> h(taps);
    const double mid = (taps - 1) / 2.0;
    double sum = 0.0;
    for (int n = 0; n < taps; ++n) {
        const double m = n - mid;
        const double sinc = (std::fabs(m) < 1e-9)
                                ? 2.0 * fc_norm
                                : std::sin(2.0 * kPi * fc_norm * m) / (kPi * m);
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * n / (taps - 1));
        h[n] = static_cast<float>(sinc * w);
        sum += h[n];
    }
    for (float& v : h) v /= static_cast<float>(sum);
    return h;
}

// Complex decimating FIR (ring delay line, dot product every M-th input).
struct FirC {
    std::vector<float> h;
    std::vector<std::complex<float>> hist;
    int M = 1, head = 0, phase = 0;

    void init(std::vector<float> taps, int decim) {
        h = std::move(taps);
        hist.assign(h.size(), {0.0f, 0.0f});
        M = decim;
        head = 0;
        phase = 0;
    }
    inline bool push(std::complex<float> x, std::complex<float>& out) {
        hist[head] = x;
        if (++head == static_cast<int>(hist.size())) head = 0;
        if (++phase < M) return false;
        phase = 0;
        const int N = static_cast<int>(h.size());
        float re = 0.0f, im = 0.0f;
        int idx = head - 1;
        if (idx < 0) idx += N;
        for (int k = 0; k < N; ++k) {
            re += h[k] * hist[idx].real();
            im += h[k] * hist[idx].imag();
            if (--idx < 0) idx += N;
        }
        out = {re, im};
        return true;
    }
};

// Real decimating FIR. M == 1 makes it a plain (non-decimating) low-pass.
struct FirR {
    std::vector<float> h;
    std::vector<float> hist;
    int M = 1, head = 0, phase = 0;

    void init(std::vector<float> taps, int decim) {
        h = std::move(taps);
        hist.assign(h.size(), 0.0f);
        M = decim;
        head = 0;
        phase = 0;
    }
    inline bool push(float x, float& out) {
        hist[head] = x;
        if (++head == static_cast<int>(hist.size())) head = 0;
        if (++phase < M) return false;
        phase = 0;
        out = dot();
        return true;
    }
    inline float filt(float x) { // M == 1 convenience
        hist[head] = x;
        if (++head == static_cast<int>(hist.size())) head = 0;
        return dot();
    }
    inline float dot() const {
        const int N = static_cast<int>(h.size());
        float acc = 0.0f;
        int idx = head - 1;
        if (idx < 0) idx += N;
        for (int k = 0; k < N; ++k) {
            acc += h[k] * hist[idx];
            if (--idx < 0) idx += N;
        }
        return acc;
    }
};

} // namespace

struct AudioDemod::Impl {
    double input_rate;

    std::atomic<int>   pending_mode{WFM};
    std::atomic<bool>  muted{false};
    std::atomic<float> volume{0.5f};
    int                mode = WFM;

    // Shared first stage: 2.4 Msps -> 240 kHz complex (decimate by ~10).
    FirC  stage1;
    int   m1 = 10;
    double r1 = 0.0; // intermediate rate

    // WFM branch.
    std::complex<float> fm_prev{0.0f, 0.0f};
    FirR  wfm_audio;          // 240k -> 48k real, audio LP
    float deemph = 0.0f, deemph_k = 0.0f;

    // Narrow branch: 240k -> 48k complex baseband.
    FirC  narrow;
    std::complex<float> nb_prev{0.0f, 0.0f};
    float am_dc = 0.0f;

    // Weaver SSB/CW.
    double weaver_theta = 0.0, weaver_dw = 0.0;
    FirR  weaver_i, weaver_q;

    // AGC (AM/SSB/CW).
    float agc_pk = 1e-3f;

    // SDL audio.
    SDL_AudioDeviceID dev = 0;
    std::vector<float> pcm; // reused per process() call

    explicit Impl(double rate) : input_rate(rate) {
        // Stage 1: decimate to ~240 kHz, cutoff ~100 kHz.
        m1 = static_cast<int>(std::lround(input_rate / 240000.0));
        if (m1 < 1) m1 = 1;
        r1 = input_rate / m1;
        stage1.init(make_lowpass(47, 100000.0 / input_rate), m1);

        build_chain(WFM);

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            SDL_AudioSpec want{}, have{};
            want.freq = kAudioHz;
            want.format = AUDIO_F32SYS;
            want.channels = 1;
            want.samples = 1024;
            want.callback = nullptr; // queued audio
            dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (dev) {
                SDL_PauseAudioDevice(dev, 0);
            }
        }
    }

    ~Impl() {
        if (dev) {
            SDL_CloseAudioDevice(dev);
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    void build_chain(int m) {
        mode = m;
        const int m2 = static_cast<int>(std::lround(r1 / kAudioHz)); // ~5
        const double fa = r1 / m2;

        if (m == WFM) {
            wfm_audio.init(make_lowpass(47, 15000.0 / r1), m2);
            deemph_k = static_cast<float>(1.0 - std::exp(-1.0 / (fa * 50e-6))); // 50us
            deemph = 0.0f;
            fm_prev = {0.0f, 0.0f};
        } else {
            // Channel filter width by mode (NFM widest, CW narrowest pre-Weaver).
            double ch = 8000.0;
            if (m == AM) ch = 5000.0;
            else if (m == USB || m == LSB) ch = 3500.0;
            else if (m == CW) ch = 3000.0;
            narrow.init(make_lowpass(47, ch / r1), m2);
            nb_prev = {0.0f, 0.0f};
            am_dc = 0.0f;

            // Weaver oscillator at the sideband half-width; arm LP ~ half-width.
            const double wc = (m == CW) ? 700.0 : 1500.0;
            weaver_dw = 2.0 * kPi * wc / fa;
            weaver_theta = 0.0;
            const double arm = (m == CW) ? 400.0 : 1700.0;
            weaver_i.init(make_lowpass(47, arm / fa), 1);
            weaver_q.init(make_lowpass(47, arm / fa), 1);
        }
        agc_pk = 1e-3f;
    }

    inline float discriminate(std::complex<float> c, std::complex<float>& prev) {
        // arg(c * conj(prev)) — instantaneous frequency.
        const float re = c.real() * prev.real() + c.imag() * prev.imag();
        const float im = c.imag() * prev.real() - c.real() * prev.imag();
        prev = c;
        return std::atan2(im, re) * static_cast<float>(1.0 / kPi);
    }

    inline float agc(float x) {
        const float a = std::fabs(x);
        if (a > agc_pk) agc_pk = a;
        else agc_pk = agc_pk * 0.9995f + a * 0.0005f;
        float g = 0.3f / (agc_pk + 1e-4f);
        if (g > 40.0f) g = 40.0f;
        return x * g;
    }

    inline float demod_narrow(std::complex<float> c) {
        switch (mode) {
            case NFM:
                return discriminate(c, nb_prev) * 3.0f;
            case AM: {
                const float env = std::abs(c);
                am_dc += (env - am_dc) * 0.001f;
                return agc(env - am_dc);
            }
            case USB:
            case LSB:
            case CW:
            default: {
                const float ct = std::cos(static_cast<float>(weaver_theta));
                const float st = std::sin(static_cast<float>(weaver_theta));
                const float i1 = c.real() * ct + c.imag() * st;
                const float q1 = c.imag() * ct - c.real() * st;
                const float il = weaver_i.filt(i1);
                const float ql = weaver_q.filt(q1);
                weaver_theta += weaver_dw;
                if (weaver_theta > 2.0 * kPi) weaver_theta -= 2.0 * kPi;
                // USB/CW: difference; LSB: sum.
                const float out = (mode == LSB) ? (il * ct + ql * st)
                                                : (il * ct - ql * st);
                return agc(out);
            }
        }
    }

    void process(const std::complex<float>* iq, size_t n) {
        const int want = pending_mode.load(std::memory_order_relaxed);
        if (want != mode) {
            build_chain(want);
        }

        pcm.clear();
        const float vol = volume.load(std::memory_order_relaxed);

        std::complex<float> s;
        for (size_t i = 0; i < n; ++i) {
            if (!stage1.push(iq[i], s)) continue;

            float a;
            bool have = false;
            if (mode == WFM) {
                const float d = discriminate(s, fm_prev) * 1.2f;
                float au;
                if (wfm_audio.push(d, au)) {
                    deemph += (au - deemph) * deemph_k;
                    a = deemph;
                    have = true;
                }
            } else {
                std::complex<float> c;
                if (narrow.push(s, c)) {
                    a = demod_narrow(c);
                    have = true;
                }
            }
            if (have) {
                float o = a * vol;
                if (o > 1.0f) o = 1.0f;
                else if (o < -1.0f) o = -1.0f;
                pcm.push_back(o);
            }
        }

        if (!dev) return;
        if (muted.load(std::memory_order_relaxed)) {
            SDL_ClearQueuedAudio(dev);
            return;
        }
        if (pcm.empty()) return;
        // Bound latency: drop if the output queue is already > ~0.3 s deep.
        if (SDL_GetQueuedAudioSize(dev) <
            static_cast<Uint32>(kAudioHz * sizeof(float) * 0.3)) {
            SDL_QueueAudio(dev, pcm.data(),
                           static_cast<Uint32>(pcm.size() * sizeof(float)));
        }
    }
};

AudioDemod::AudioDemod(double input_rate)
    : impl_(std::make_unique<Impl>(input_rate)) {}

AudioDemod::~AudioDemod() = default;

void AudioDemod::set_mode(int mode) {
    impl_->pending_mode.store(mode, std::memory_order_relaxed);
}
void AudioDemod::set_muted(bool muted) {
    impl_->muted.store(muted, std::memory_order_relaxed);
}
void AudioDemod::set_volume(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    impl_->volume.store(vol, std::memory_order_relaxed);
}
void AudioDemod::process(const std::complex<float>* iq, size_t n) {
    impl_->process(iq, n);
}

} // namespace sdr

#else // !SDR_HAVE_AUDIO — no-op audio (spectrum-only device build, or mock)

namespace sdr {
struct AudioDemod::Impl {};
AudioDemod::AudioDemod(double) {}
AudioDemod::~AudioDemod() = default;
void AudioDemod::set_mode(int) {}
void AudioDemod::set_muted(bool) {}
void AudioDemod::set_volume(float) {}
void AudioDemod::process(const std::complex<float>*, size_t) {}
} // namespace sdr

#endif // SDR_HAVE_AUDIO
