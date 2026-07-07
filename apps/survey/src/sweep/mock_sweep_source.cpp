/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "mock_sweep_source.h"

#include <algorithm>
#include <cmath>

namespace survey {

MockSweepSource::MockSweepSource(uint32_t seed) : rng_state_(seed ? seed : 1u) {
    for (int c = 0; c < kCarriers; ++c) {
        carriers_[c].pos   = 0.15f + 0.3f * static_cast<float>(c);
        carriers_[c].drift = (c % 2 == 0 ? 1.0f : -1.0f) * (0.0004f + 0.0003f * c);
        carriers_[c].level = 0.95f - 0.2f * static_cast<float>(c);
        carriers_[c].width = 0.008f + 0.004f * static_cast<float>(c);
    }
}

float MockSweepSource::rand01() {
    rng_state_ = rng_state_ * 1664525u + 1013904223u; // LCG, deterministic
    return static_cast<float>(rng_state_ >> 8) / 16777216.0f;
}

void MockSweepSource::next_frame(float* mags, int n_bins) {
    if (!mags || n_bins <= 0) return;

    float peak = 0.0f;
    for (int i = 0; i < n_bins; ++i) {
        mags[i] = 0.05f + 0.05f * rand01(); // noise floor
    }
    const float span = static_cast<float>(std::max(n_bins - 1, 1)); // avoid /0 at n_bins==1
    for (auto& carrier : carriers_) {
        carrier.pos += carrier.drift;
        if (carrier.pos < 0.05f || carrier.pos > 0.95f) carrier.drift = -carrier.drift;
        for (int i = 0; i < n_bins; ++i) {
            const float x = static_cast<float>(i) / span;
            const float d = (x - carrier.pos) / carrier.width;
            mags[i] += carrier.level * std::exp(-0.5f * d * d);
        }
    }
    for (int i = 0; i < n_bins; ++i) {
        if (mags[i] > 1.0f) mags[i] = 1.0f;
        if (mags[i] > peak) peak = mags[i];
    }
    last_peak_ = peak;
}

std::vector<SweepPeak> MockSweepSource::peaks() const {
    std::vector<SweepPeak> out;
    const double span = static_cast<double>(stop_hz_ - start_hz_);
    for (const auto& carrier : carriers_) {
        SweepPeak p;
        p.freq_hz = start_hz_ + static_cast<int64_t>(span * carrier.pos);
        p.dbm = -80.0f + 50.0f * carrier.level; // plausible absolute readout
        out.push_back(p);
    }
    return out;
}

void MockSweepSource::set_range(int64_t start_hz, int64_t stop_hz, int32_t) {
    if (start_hz < stop_hz) {
        start_hz_ = start_hz;
        stop_hz_ = stop_hz;
    }
}

} // namespace survey
