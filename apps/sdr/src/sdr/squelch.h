/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

namespace sdr {

// Time-based signal gate. All times are monotonic milliseconds supplied by the
// caller; this class has no clock, audio, UI, or device dependencies.
class Squelch {
public:
    explicit Squelch(int threshold = 0, int hysteresis = 8, int64_t hold_ms = 2500);

    void set_threshold(int threshold);
    void set_hysteresis(int hysteresis);
    void set_hold_ms(int64_t hold_ms);

    int threshold() const { return threshold_; }
    int hysteresis() const { return hysteresis_; }
    int64_t hold_ms() const { return hold_ms_; }

    void update(int level, int64_t now_ms);
    bool open() const { return open_; }

private:
    int threshold_ = 0;
    int hysteresis_ = 8;
    int64_t hold_ms_ = 2500;
    bool open_ = true;
    std::optional<int64_t> below_since_ms_;
};

// The signal level in the tuned channel, 0..100: the strongest of `n_bins`
// display magnitudes (each in [0, 1], spread evenly over `span_hz` and centred
// on the VFO) that fall inside the passband [low_hz, high_hz] around the VFO.
// The centre bin always counts, so a passband narrower than one bin still
// reads a level. The S-meter and the squelch both use it.
int channel_level(const float* mags, int n_bins, double span_hz, int32_t low_hz, int32_t high_hz);

} // namespace sdr
