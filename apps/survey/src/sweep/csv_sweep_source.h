/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "sweep_source.h"

#include <cstdint>
#include <memory>

namespace survey {

// Live sweep source: shells out to rtl_power (RTL-SDR, <=~1.7 GHz) or
// hackrf_sweep (HackRF, -> 6 GHz) and feeds their CSV stdout through the pure
// SweepAccumulator on a worker thread. The latest completed sweep is published
// under a lock; `next_frame` copies and auto-scales it (floor-follow, same
// behavior as the SDR app's RtlTcpSource) without blocking. The child is
// respawned with backoff on exit; `ok()` is false until a sweep is live.
// All subprocess/thread state lives in the .cpp (pImpl).
class CsvSweepSource final : public SweepSource {
public:
    enum class Tool { RtlPower, HackrfSweep };

    CsvSweepSource(Tool tool, int64_t start_hz, int64_t stop_hz, int32_t bin_hz);
    ~CsvSweepSource() override;

    CsvSweepSource(const CsvSweepSource&) = delete;
    CsvSweepSource& operator=(const CsvSweepSource&) = delete;

    void next_frame(float* mags, int n_bins) override;
    float last_peak() const override;
    std::vector<SweepPeak> peaks() const override;
    void set_range(int64_t start_hz, int64_t stop_hz, int32_t bin_hz) override;
    bool ok() const override;
    int64_t start_hz() const override;
    int64_t stop_hz() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace survey
