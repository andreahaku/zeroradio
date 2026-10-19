/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace sdr {

// The audio chain decimates the IQ by a whole factor to ~240 kHz, then by a
// whole factor to the 48 kHz output. Only a multiple of this step comes out
// at exactly 48 kHz.
constexpr uint32_t kAudioRateStep = 240000;
constexpr double   kAudioOutHz = 48000.0;

// The two decimation factors AudioDemod uses for an input rate, and the
// audio rate they produce. The output device plays kAudioOutHz: any other
// rate overruns it (or starves it) and the audio drops chunks.
struct AudioDecimation {
    int    m1;       // input -> ~240 kHz
    int    m2;       // ~240 kHz -> ~48 kHz
    double mid_hz;   // input / m1
    double audio_hz; // mid_hz / m2
};
AudioDecimation audio_decimation(double input_rate);

// RTL capture rate for a visible span (the zoom): the narrowest rate that
// covers the span, within `cap` (the host's drain ceiling, SDR_SAMPLE_RATE)
// and the RTL2832U's valid ranges, and a multiple of kAudioRateStep.
uint32_t rate_for_span(int32_t span_hz, uint32_t cap);

} // namespace sdr
