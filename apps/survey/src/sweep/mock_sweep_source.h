/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "sweep_source.h"

#include <cstdint>

namespace survey {

// Synthetic sweep: a noisy wideband floor plus a few carriers drifting slowly
// across the range (mirrors the SDR app's MockSpectrumSource), so the survey
// view runs with no sweep tool or hardware.
class MockSweepSource final : public SweepSource {
public:
    explicit MockSweepSource(uint32_t seed = 0x51E77A0Bu);

    void next_frame(float* mags, int n_bins) override;
    float last_peak() const override { return last_peak_; }
    std::vector<SweepPeak> peaks() const override;
    void set_range(int64_t start_hz, int64_t stop_hz, int32_t bin_hz) override;
    bool ok() const override { return true; }
    int64_t start_hz() const override { return start_hz_; }
    int64_t stop_hz() const override { return stop_hz_; }

private:
    struct Carrier {
        float pos;      // position in [0,1] across the range
        float drift;    // per-frame position delta
        float level;    // magnitude [0,1]
        float width;    // gaussian width in [0,1] units
    };
    static constexpr int kCarriers = 3;

    uint32_t rng_state_;
    float rand01();

    int64_t start_hz_ = 88000000;
    int64_t stop_hz_  = 108000000;
    Carrier carriers_[kCarriers];
    float last_peak_ = 0.0f;
};

} // namespace survey
