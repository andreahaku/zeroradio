/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 *
 * Frozen parity test for the pure sweep CSV parser/accumulator
 * (apps/survey/src/sweep/sweep_accumulator). Every expectation in
 * sweep_parser_vectors.inc was derived by an independent python
 * implementation of the parsing contract documented in sweep_accumulator.h
 * (gen_sweep_vectors.py, committed in this directory) — the two-source
 * oracle pattern of the AIS and Meshtastic decoder tests. This file is the
 * immutable reward: it is not edited to make code pass.
 *
 * Coverage: rtl_power segment stitching + wrap detection, one-shot flush,
 * hackrf_sweep-shaped wide rows, malformed-row skipping, non-finite dB bins
 * -> kNoDataDbm, byte-chunked feeding, resample-to-N-cells spot values and
 * exact peak lists (freq + dBm, ordered), plus the CAPTURED real FM sweep.
 */

#include "sweep_accumulator.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "sweep_parser_vectors.inc"

namespace {

int g_fails = 0;

void check(bool ok, const char* name, const char* what) {
    if (!ok) {
        std::printf("  FAIL %-20s %s\n", name, what);
        ++g_fails;
    }
}

bool fclose_to(float a, float b) { return std::fabs(a - b) <= 0.05f; }

} // namespace

using survey::SweepAccumulator;

int main() {
    constexpr int kMaxBins = 512;
    float frame[kMaxBins];

    for (const auto& v : kSweepVectors) {
        SweepAccumulator acc(v.start_hz, v.stop_hz);
        check(acc.start_hz() == v.start_hz && acc.stop_hz() == v.stop_hz,
              v.name, "start/stop getters");

        int completed = 0;
        if (v.chunk <= 0) {
            completed += acc.feed(v.csv, v.csv_len);
        } else {
            for (unsigned long off = 0; off < v.csv_len;
                 off += static_cast<unsigned long>(v.chunk)) {
                const unsigned long n =
                    (off + v.chunk <= v.csv_len) ? v.chunk : v.csv_len - off;
                completed += acc.feed(v.csv + off, n);
            }
        }
        if (v.do_flush) completed += acc.flush();

        check(completed == v.exp_sweeps, v.name, "sweeps completed (feed+flush)");
        check(acc.sweeps() == v.exp_sweeps, v.name, "sweeps() getter");
        check(acc.has_frame() == v.exp_frame, v.name, "has_frame()");

        for (unsigned long s = 0; s < v.n_spots; ++s) {
            const SpotExpect& e = v.spots[s];
            const bool got = acc.frame_dbm(frame, e.n_bins);
            check(got == v.exp_frame, v.name, "frame_dbm() availability");
            if (!got) continue;
            char what[64];
            std::snprintf(what, sizeof(what), "resampled cell [%d/%d]", e.idx, e.n_bins);
            check(fclose_to(frame[e.idx], e.dbm), v.name, what);
        }

        const auto peaks = acc.peaks(v.peak_threshold);
        check(peaks.size() == v.n_peaks, v.name, "peak count");
        for (unsigned long p = 0; p < v.n_peaks && p < peaks.size(); ++p) {
            char what[64];
            std::snprintf(what, sizeof(what), "peak[%lu] freq", p);
            check(peaks[p].freq_hz == v.peaks[p].freq_hz, v.name, what);
            std::snprintf(what, sizeof(what), "peak[%lu] dbm", p);
            check(fclose_to(peaks[p].dbm, v.peaks[p].dbm), v.name, what);
        }

        if (v.exp_frame) {
            // max_peaks truncation keeps the strongest (list prefix)...
            if (v.n_peaks >= 2) {
                const auto top1 = acc.peaks(v.peak_threshold, 1);
                check(top1.size() == 1, v.name, "max_peaks=1 truncates");
                if (!top1.empty())
                    check(top1[0].freq_hz == v.peaks[0].freq_hz, v.name,
                          "max_peaks=1 keeps the strongest");
            }
            // ...and an impossible threshold yields no peaks at all.
            check(acc.peaks(500.0f).empty(), v.name, "impossible threshold -> no peaks");
        }

        // A frame must never be reported before the first publish.
        SweepAccumulator empty(v.start_hz, v.stop_hz);
        check(!empty.has_frame() && !empty.frame_dbm(frame, 8), v.name,
              "no frame before first sweep");
        check(empty.peaks(v.peak_threshold).empty(), v.name,
              "no peaks before first sweep");
    }

    const int total = static_cast<int>(sizeof(kSweepVectors) / sizeof(kSweepVectors[0]));
    if (g_fails == 0) {
        std::printf("Sweep parser: %d vectors OK\n", total);
        return 0;
    }
    std::printf("Sweep parser: %d assertion(s) FAILED\n", g_fails);
    return 1;
}
