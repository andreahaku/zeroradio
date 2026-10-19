/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "squelch.h"

#include <algorithm>
#include <cmath>

namespace sdr {

Squelch::Squelch(int threshold, int hysteresis, int64_t hold_ms) {
    set_hysteresis(hysteresis);
    set_hold_ms(hold_ms);
    set_threshold(threshold);
}

void Squelch::set_threshold(int threshold) {
    const int next = std::clamp(threshold, 0, 100);
    if (next == threshold_) return;
    const bool was_disabled = threshold_ == 0;
    threshold_ = next;
    below_since_ms_.reset();
    if (next == 0) open_ = true;
    else if (was_disabled) open_ = false;
}

void Squelch::set_hysteresis(int hysteresis) {
    hysteresis_ = std::clamp(hysteresis, 0, 100);
    below_since_ms_.reset();
}

void Squelch::set_hold_ms(int64_t hold_ms) {
    hold_ms_ = std::max<int64_t>(0, hold_ms);
    below_since_ms_.reset();
}

void Squelch::update(int level, int64_t now_ms) {
    if (threshold_ == 0) {
        open_ = true;
        below_since_ms_.reset();
        return;
    }

    level = std::clamp(level, 0, 100);
    if (!open_) {
        if (level >= threshold_) open_ = true;
        below_since_ms_.reset();
        return;
    }

    // Keep at least one level below the close threshold even when a very low
    // opening threshold is paired with a larger hysteresis setting.
    const int close_below = std::max(1, threshold_ - hysteresis_);
    if (level >= close_below) {
        below_since_ms_.reset();
        return;
    }

    if (!below_since_ms_ || now_ms < *below_since_ms_) below_since_ms_ = now_ms;
    if (now_ms - *below_since_ms_ >= hold_ms_) {
        open_ = false;
        below_since_ms_.reset();
    }
}

int channel_level(const float* mags, int n_bins, double span_hz, int32_t low_hz, int32_t high_hz) {
    if (!mags || n_bins <= 0 || span_hz <= 0.0) return 0;
    const double hz_per_bin = span_hz / static_cast<double>(n_bins);
    const double centre = n_bins / 2.0;
    int lo = static_cast<int>(std::floor(centre + low_hz / hz_per_bin));
    int hi = static_cast<int>(std::ceil(centre + high_hz / hz_per_bin)) - 1;
    const int centre_bin = std::min(n_bins - 1, static_cast<int>(centre));
    lo = std::clamp(std::min(lo, centre_bin), 0, n_bins - 1);
    hi = std::clamp(std::max(hi, centre_bin), 0, n_bins - 1);
    float peak = 0.0f;
    for (int i = lo; i <= hi; ++i) peak = std::max(peak, mags[i]);
    return static_cast<int>(std::lround(std::clamp(peak, 0.0f, 1.0f) * 100.0f));
}

} // namespace sdr
