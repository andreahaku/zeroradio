/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "spectrum_source.h"

#include <cmath>

namespace sdr {
namespace {

struct Carrier {
    float center_norm;   // base position in [0, 1]
    float drift_amp;     // peak excursion in normalized units
    float drift_rate;    // radians per frame
    float drift_phase;   // phase offset
    float width;         // gaussian sigma in normalized units
    float amplitude;     // peak magnitude in [0, 1]
};

// Two carriers that sweep across the band at different rates so the waterfall
// shows crossing/independent traces.
constexpr Carrier kCarriers[] = {
    {0.32f, 0.22f, 0.018f, 0.0f,  0.012f, 0.95f},
    {0.68f, 0.16f, 0.011f, 1.7f,  0.020f, 0.80f},
};

} // namespace

MockSpectrumSource::MockSpectrumSource(uint32_t seed) : rng_state_(seed ? seed : 1u) {}

float MockSpectrumSource::frand() {
    // xorshift32 -> [0, 1)
    uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void MockSpectrumSource::next_frame(float* mags, int n_bins) {
    if (!mags || n_bins <= 0) {
        return;
    }

    phase_ += 1.0f;

    float peak = 0.0f;
    const float inv_bins = 1.0f / static_cast<float>(n_bins);

    for (int i = 0; i < n_bins; ++i) {
        const float pos = static_cast<float>(i) * inv_bins; // [0, 1)

        // Noise floor: low, slightly textured.
        float value = 0.04f + 0.06f * frand();

        // Add the drifting carriers as gaussian bumps.
        for (const auto& c : kCarriers) {
            const float center =
                c.center_norm + c.drift_amp * std::sin(phase_ * c.drift_rate + c.drift_phase);
            const float d = (pos - center) / c.width;
            value += c.amplitude * std::exp(-0.5f * d * d);
        }

        if (value > 1.0f) {
            value = 1.0f;
        }
        mags[i] = value;
        if (value > peak) {
            peak = value;
        }
    }

    last_peak_ = peak;
}

} // namespace sdr
