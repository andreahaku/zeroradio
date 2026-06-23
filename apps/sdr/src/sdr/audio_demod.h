/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>  // ensures __intmax_t is defined before <math.h>/mathcalls.h
#include <complex>
#include <cstddef>
#include <memory>

namespace sdr {

// Real-time audio demodulator. Fed the continuous complex baseband (centred on
// the VFO) by the RTL-SDR reader thread, it demodulates per mode, resamples to
// 48 kHz mono and plays through SDL audio. Non-blocking; drops audio if the
// output queue backs up. All SDL/DSP state lives in the .cpp (pImpl).
//
// Mode index matches model::RadioMode order: 0=WFM 1=FM 2=AM 3=USB 4=LSB 5=CW.
class AudioDemod {
public:
    explicit AudioDemod(double input_rate);
    ~AudioDemod();

    AudioDemod(const AudioDemod&) = delete;
    AudioDemod& operator=(const AudioDemod&) = delete;

    void set_mode(int mode);     // rebuilds the demod chain
    void set_muted(bool muted);
    void set_volume(float vol);  // 0..1 master gain

    // Demodulate `n` complex baseband samples at input_rate and queue audio.
    void process(const std::complex<float>* iq, size_t n);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sdr
