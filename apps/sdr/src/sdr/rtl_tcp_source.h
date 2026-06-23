/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "spectrum_source.h"

#include <cstdint>
#include <memory>
#include <string>

namespace sdr {

// Real spectrum source backed by the stock `rtl_tcp` server (RTL-SDR dongle).
//
// A background thread connects to rtl_tcp, configures sample rate / gain,
// streams the raw 8-bit IQ, runs an FFT (fftw3f) and publishes the latest
// magnitude spectrum (in dB) under a lock. `next_frame()` crops the central
// span window and normalizes to [0, 1] without blocking. `set_tuning()` retunes
// the hardware (SET_FREQUENCY) when the VFO moves and selects the zoom window.
//
// Connection is resilient: if rtl_tcp is down the thread retries and the frame
// stays blank, so the UI never blocks or crashes. All FFT/socket state lives in
// the .cpp (pImpl) to keep fftw out of this header.
class RtlTcpSource final : public SpectrumSource {
public:
    RtlTcpSource(std::string host, uint16_t port, int64_t initial_center_hz);
    ~RtlTcpSource() override;

    RtlTcpSource(const RtlTcpSource&) = delete;
    RtlTcpSource& operator=(const RtlTcpSource&) = delete;

    void next_frame(float* mags, int n_bins) override;
    float last_peak() const override;
    void set_tuning(int64_t center_hz, int32_t span_hz) override;
    void set_mode(int mode) override;  // routes to the audio demodulator
    void set_volume(float vol) override;
    void set_muted(bool muted) override;
    void set_gain(bool automatic, int tenth_db) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sdr
