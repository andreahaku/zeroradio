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

} // namespace sdr
