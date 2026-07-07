/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "csv_sweep_source.h"

#ifdef SURVEY_HAVE_SWEEP

#include "sweep_accumulator.h"
#include "subprocess.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace survey {
namespace {

// Auto-scaling to a slowly-following noise floor (same behavior/constants as
// the SDR app's RtlTcpSource, so the shared colormap reads identically).
constexpr float kDynRangeDb = 50.0f; // dB above the floor -> 1.0
constexpr float kFloorEma   = 0.05f; // floor follow rate per frame

// The published full-range frame resolution (one bin per canvas column).
constexpr int kFrameBins = 320;

// Peaks: local maxima this many dB above the sweep median, capped per sweep.
constexpr float kPeakThresholdDb = 8.0f;
constexpr int   kMaxPeaks        = 16;

// Respawn backoff when the tool exits/fails (dongle missing, bad args, ...).
constexpr auto kRespawnDelay = std::chrono::seconds(2);

std::vector<std::string> tool_argv(CsvSweepSource::Tool tool,
                                   int64_t start_hz, int64_t stop_hz, int32_t bin_hz) {
    if (tool == CsvSweepSource::Tool::RtlPower) {
        // rtl_power -f start:stop:bin -i 1 -  (continuous CSV sweeps to stdout)
        char range[64];
        std::snprintf(range, sizeof(range), "%lld:%lld:%d",
                      static_cast<long long>(start_hz),
                      static_cast<long long>(stop_hz), bin_hz);
        return {"rtl_power", "-f", range, "-i", "1", "-"};
    }
    // hackrf_sweep -f startMHz:stopMHz -w bin  (CSV to stdout)
    char range[64];
    std::snprintf(range, sizeof(range), "%lld:%lld",
                  static_cast<long long>(start_hz / 1000000),
                  static_cast<long long>(stop_hz / 1000000));
    char width[16];
    std::snprintf(width, sizeof(width), "%d", bin_hz);
    return {"hackrf_sweep", "-f", range, "-w", width};
}

} // namespace

struct CsvSweepSource::Impl {
    const Tool tool;

    // Desired range; `generation` bumps to make the worker rebuild the child.
    std::atomic<int64_t> want_start;
    std::atomic<int64_t> want_stop;
    std::atomic<int32_t> want_bin;
    std::atomic<int>     generation{0};

    std::atomic<bool> running{true};
    std::atomic<bool> sweep_ok{false};
    std::thread worker;

    // Latest published sweep (raw dBm across the CURRENT range) + its peaks.
    mutable std::mutex frame_mutex;
    std::vector<float> shared_dbm;       // kFrameBins, valid when have_frame
    std::vector<SweepPeak> shared_peaks;
    int64_t frame_start = 0;
    int64_t frame_stop = 0;
    bool have_frame = false;

    // UI-thread scratch + floor-follow state (touched only by next_frame).
    std::vector<float> scratch_dbm;
    float floor_ema = 0.0f;
    bool floor_init = false;
    float last_peak = 0.0f;

    Impl(Tool t, int64_t start_hz, int64_t stop_hz, int32_t bin_hz)
        : tool(t), want_start(start_hz), want_stop(stop_hz), want_bin(bin_hz),
          shared_dbm(kFrameBins, kNoDataDbm), scratch_dbm(kFrameBins, kNoDataDbm) {
        worker = std::thread([this] { run(); });
    }

    ~Impl() {
        running.store(false);
        if (worker.joinable()) worker.join();
    }

    // Publish a completed sweep, but only if it still belongs to the current
    // range: a set_range() during the sweep bumps `generation`, and this drops
    // the now-stale frame instead of overwriting the freshly-blanked state.
    void publish(const SweepAccumulator& acc, int my_generation) {
        std::lock_guard<std::mutex> lock(frame_mutex);
        if (generation.load() != my_generation) return;
        acc.frame_dbm(shared_dbm.data(), kFrameBins);
        shared_peaks = acc.peaks(kPeakThresholdDb, kMaxPeaks);
        frame_start = acc.start_hz();
        frame_stop = acc.stop_hz();
        have_frame = true;
        sweep_ok.store(true);
    }

    // Sleep in short steps so shutdown/range changes stay responsive.
    bool wait(std::chrono::milliseconds total, int my_generation) {
        auto left = total;
        while (left.count() > 0 && running.load() &&
               generation.load() == my_generation) {
            const auto step = std::min(left, std::chrono::milliseconds(100));
            std::this_thread::sleep_for(step);
            left -= step;
        }
        return running.load() && generation.load() == my_generation;
    }

    void run() {
        std::vector<char> buf(1 << 14); // 16 KiB read chunk
        while (running.load()) {
            const int my_generation = generation.load();
            const int64_t start = want_start.load();
            const int64_t stop = want_stop.load();
            const int32_t bin = want_bin.load();

            SweepAccumulator acc(start, stop);
            toolkit::Subprocess child;
            if (!child.start(tool_argv(tool, start, stop, bin))) {
                sweep_ok.store(false);
                if (!wait(kRespawnDelay, my_generation)) continue;
                continue;
            }

            while (running.load() && generation.load() == my_generation) {
                const ssize_t n = child.read_stdout(buf.data(), buf.size());
                if (n < 0) break; // child exited (EOF)
                if (n == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                if (acc.feed(buf.data(), static_cast<size_t>(n)) > 0) {
                    publish(acc, my_generation);
                }
            }
            child.stop();

            // One-shot runs end without a wrap: publish what accumulated — but
            // only if the range is still current (skip a partial frame we exited
            // for because the user retuned).
            if (generation.load() == my_generation && acc.flush() > 0) {
                publish(acc, my_generation);
            }

            if (generation.load() == my_generation) {
                // The tool died on its own: flag it and retry after a pause.
                sweep_ok.store(false);
                wait(kRespawnDelay, my_generation);
            }
        }
    }
};

CsvSweepSource::CsvSweepSource(Tool tool, int64_t start_hz, int64_t stop_hz, int32_t bin_hz)
    : impl_(std::make_unique<Impl>(tool, start_hz, stop_hz, bin_hz)) {}

CsvSweepSource::~CsvSweepSource() = default;

void CsvSweepSource::next_frame(float* mags, int n_bins) {
    if (!mags || n_bins <= 0) return;

    bool have;
    {
        std::lock_guard<std::mutex> lock(impl_->frame_mutex);
        have = impl_->have_frame;
        if (have) impl_->scratch_dbm = impl_->shared_dbm;
    }
    if (!have) {
        std::fill(mags, mags + n_bins, 0.0f);
        impl_->last_peak = 0.0f;
        return;
    }

    // Resample kFrameBins -> n_bins (mean per column) as raw dBm first.
    const auto& dbm = impl_->scratch_dbm;
    float frame_min = 1e30f;
    for (int j = 0; j < n_bins; ++j) {
        int lo = static_cast<int>(static_cast<int64_t>(kFrameBins) * j / n_bins);
        int hi = static_cast<int>(static_cast<int64_t>(kFrameBins) * (j + 1) / n_bins);
        if (hi <= lo) hi = lo + 1;
        if (hi > kFrameBins) hi = kFrameBins;
        float sum = 0.0f;
        int used = 0;
        for (int k = lo; k < hi; ++k) {
            if (dbm[static_cast<size_t>(k)] > kNoDataDbm) {
                sum += dbm[static_cast<size_t>(k)];
                ++used;
            }
        }
        const float v = used > 0 ? sum / static_cast<float>(used) : kNoDataDbm;
        mags[j] = v;
        if (used > 0 && v < frame_min) frame_min = v;
    }
    if (frame_min > 1e29f) frame_min = kNoDataDbm; // whole frame uncovered

    if (!impl_->floor_init) {
        impl_->floor_ema = frame_min;
        impl_->floor_init = true;
    } else {
        impl_->floor_ema += (frame_min - impl_->floor_ema) * kFloorEma;
    }

    const float floor = impl_->floor_ema;
    float peak = 0.0f;
    for (int j = 0; j < n_bins; ++j) {
        float v = (mags[j] - floor) / kDynRangeDb;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        mags[j] = v;
        if (v > peak) peak = v;
    }
    impl_->last_peak = peak;
}

float CsvSweepSource::last_peak() const { return impl_->last_peak; }

std::vector<SweepPeak> CsvSweepSource::peaks() const {
    std::lock_guard<std::mutex> lock(impl_->frame_mutex);
    if (!impl_->have_frame) return {}; // blanked after a retune: no stale peaks
    return impl_->shared_peaks;
}

void CsvSweepSource::set_range(int64_t start_hz, int64_t stop_hz, int32_t bin_hz) {
    if (start_hz >= stop_hz || bin_hz <= 0) return;
    if (start_hz == impl_->want_start.load() && stop_hz == impl_->want_stop.load() &&
        bin_hz == impl_->want_bin.load()) {
        return;
    }
    impl_->want_start.store(start_hz);
    impl_->want_stop.store(stop_hz);
    impl_->want_bin.store(bin_hz);
    {
        // Blank the frame AND drop the old peaks, and bump the generation under
        // the same lock so a concurrent publish() sees the new generation and
        // discards its stale-range frame (see publish()).
        std::lock_guard<std::mutex> lock(impl_->frame_mutex);
        impl_->have_frame = false;
        impl_->shared_peaks.clear();
        impl_->generation.fetch_add(1); // worker rebuilds the child
    }
    impl_->sweep_ok.store(false);
    impl_->floor_init = false;
}

bool CsvSweepSource::ok() const { return impl_->sweep_ok.load(); }
int64_t CsvSweepSource::start_hz() const { return impl_->want_start.load(); }
int64_t CsvSweepSource::stop_hz() const { return impl_->want_stop.load(); }

} // namespace survey

#else // !SURVEY_HAVE_SWEEP

namespace survey {

struct CsvSweepSource::Impl {}; // complete type so unique_ptr<Impl> can destruct

CsvSweepSource::CsvSweepSource(Tool, int64_t, int64_t, int32_t) {}
CsvSweepSource::~CsvSweepSource() = default;
void CsvSweepSource::next_frame(float* mags, int n_bins) {
    if (mags && n_bins > 0) {
        for (int i = 0; i < n_bins; ++i) mags[i] = 0.0f;
    }
}
float CsvSweepSource::last_peak() const { return 0.0f; }
std::vector<SweepPeak> CsvSweepSource::peaks() const { return {}; }
void CsvSweepSource::set_range(int64_t, int64_t, int32_t) {}
bool CsvSweepSource::ok() const { return false; }
int64_t CsvSweepSource::start_hz() const { return 0; }
int64_t CsvSweepSource::stop_hz() const { return 0; }

} // namespace survey

#endif // SURVEY_HAVE_SWEEP
