/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace survey {

// Sentinel for "no usable power here": unparsable/non-finite dB bins and
// resample cells no segment ever covered.
inline constexpr float kNoDataDbm = -150.0f;

// One detected transmission in the latest completed sweep.
struct SweepPeak {
    int64_t freq_hz = 0; // bin centre frequency
    float dbm = 0.0f;
};

// Pure stream parser + full-range accumulator for the CSV that rtl_power and
// hackrf_sweep write to stdout. No subprocess, sockets or LVGL — it consumes
// raw bytes (any chunking) and publishes one full-range dBm frame per completed
// sweep, so it links into a standalone unit test against frozen vectors.
//
// Wire format (both tools): one row per frequency segment,
//   date, time, hz_low, hz_high, hz_step, n_samples, dB, dB, ...
// with (hz_high-hz_low)/hz_step dB bins covering [hz_low, hz_high).
//
// Parsing semantics (the frozen contract — the test oracle mirrors these):
//  - lines end at '\n'; a trailing '\r' is stripped (CRLF tolerated). feed()
//    buffers a trailing partial line across calls; flush() first consumes any
//    buffered partial line as if it were newline-terminated.
//  - bin i of a row has centre frequency  hz_low + hz_step*i + hz_step/2;
//    bins whose centre falls outside [start_hz, stop_hz) are dropped.
//  - numeric fields parse FULL-TOKEN (the whole comma-separated field, after
//    trimming spaces, must be consumed) — "100abc" is not a number.
//  - a row is MALFORMED and skipped whole when it has fewer than 7 fields, or
//    hz_low/hz_high/hz_step do not full-token-parse as finite numbers with
//    hz_low >= 0, hz_step > 0 and hz_low < hz_high. A dB field that does not
//    full-token-parse, or parses non-finite (inf/nan), contributes kNoDataDbm
//    for that bin only.
//  - rows accumulate points (centre, dBm); a point at an already-seen centre
//    overwrites (last wins).
//  - WRAP RULE: a valid row whose hz_low is LOWER than the previous accepted
//    row's hz_low completes the sweep — the accumulated points are published
//    as one frame, the accumulator resets, then the new row is added to the
//    next sweep. feed() returns how many sweeps completed during the call;
//    later frames replace earlier ones (only the latest is readable).
//  - flush() publishes any pending points as a final frame (rtl_power -1
//    one-shot mode ends without wrapping); returns 1 if it published.
//  - frame_dbm(n): the latest published frame resampled to n equal cells over
//    [start_hz, stop_hz): each cell = arithmetic mean of the points whose
//    centre lies in it, kNoDataDbm when a cell holds no point.
//  - peaks(t, max): over the latest published frame's points sorted by centre:
//    point i (not first/last) is a peak when dbm[i] > dbm[i-1] and
//    dbm[i] >= dbm[i+1] and dbm[i] >= median + t, where median is computed
//    over ALL point dBm values (kNoDataDbm sentinels included) as the middle
//    value for odd counts and the arithmetic mean of the two middle values
//    for even counts. Result sorted by dBm descending (ties: lower frequency
//    first), truncated to max entries.
class SweepAccumulator {
public:
    SweepAccumulator(int64_t start_hz, int64_t stop_hz);
    ~SweepAccumulator();

    SweepAccumulator(const SweepAccumulator&) = delete;
    SweepAccumulator& operator=(const SweepAccumulator&) = delete;

    // Consume a chunk of CSV stdout (partial lines are buffered across calls).
    // Returns the number of sweeps completed during this call.
    int feed(const char* data, size_t len);

    // Publish pending points as a final frame (no-op when empty). 1 if published.
    int flush();

    int sweeps() const;      // total published sweeps
    bool has_frame() const;  // at least one frame published

    // Latest frame resampled to n_bins dBm values, low -> high edge.
    // False (and untouched output) when no frame has been published yet.
    bool frame_dbm(float* dbm, int n_bins) const;

    std::vector<SweepPeak> peaks(float threshold_db, int max_peaks = 16) const;

    int64_t start_hz() const;
    int64_t stop_hz() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace survey
