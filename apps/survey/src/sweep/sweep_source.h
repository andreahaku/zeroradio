/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "sweep_accumulator.h"

#include <cstdint>
#include <vector>

namespace survey {

// Abstract source of wide-band sweep frames. Same tick contract as the SDR
// app's SpectrumSource: `next_frame` fills n_bins magnitudes normalized to
// [0, 1] (low edge -> high edge) and must not block; the survey adds the
// absolute peak list from the latest completed sweep for the PEAKS view.
class SweepSource {
public:
    virtual ~SweepSource() = default;

    // Fill `mags` with n_bins values in [0, 1]. Must not block; blank frame
    // (all zeros) while no sweep has been published yet.
    virtual void next_frame(float* mags, int n_bins) = 0;

    // Peak magnitude of the most recent frame, in [0, 1] (drives the S-meter).
    virtual float last_peak() const { return 0.0f; }

    // Transmissions detected in the latest completed sweep (absolute dBm).
    virtual std::vector<SweepPeak> peaks() const = 0;

    // Reconfigure the swept range. bin_hz is a resolution hint; backends clamp.
    virtual void set_range(int64_t start_hz, int64_t stop_hz, int32_t bin_hz) = 0;

    // True once at least one full sweep has been published.
    virtual bool ok() const = 0;

    virtual int64_t start_hz() const = 0;
    virtual int64_t stop_hz() const = 0;
};

} // namespace survey
