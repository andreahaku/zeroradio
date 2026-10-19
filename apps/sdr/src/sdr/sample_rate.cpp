/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "sample_rate.h"

#include <cmath>

namespace sdr {

AudioDecimation audio_decimation(double input_rate) {
    AudioDecimation d{};
    d.m1 = static_cast<int>(std::lround(input_rate / kAudioRateStep));
    if (d.m1 < 1) d.m1 = 1;
    d.mid_hz = input_rate / d.m1;
    d.m2 = static_cast<int>(std::lround(d.mid_hz / kAudioOutHz));
    if (d.m2 < 1) d.m2 = 1;
    d.audio_hz = d.mid_hz / d.m2;
    return d;
}

uint32_t rate_for_span(int32_t span_hz, uint32_t cap) {
    // The RTL2832U takes 225-300 kHz or 900 kHz-3.2 MHz. The multiples of
    // kAudioRateStep inside those ranges are 240 kHz and 960 kHz to 3.12 MHz.
    constexpr uint32_t kLowRate = kAudioRateStep;     // 240 kHz
    constexpr uint32_t kMinRate = 4 * kAudioRateStep; // 960 kHz
    constexpr uint32_t kMaxRate = 13 * kAudioRateStep; // 3.12 MHz
    uint32_t top = cap / kAudioRateStep * kAudioRateStep; // round the cap down
    if (top > kMaxRate) top = kMaxRate;
    // A cap under the 900 kHz floor: 240 kHz, the low band's only multiple.
    // Under 240 kHz no multiple fits, and the cap wins over exact audio.
    if (top < kMinRate) return cap >= kLowRate ? kLowRate : cap;
    if (span_hz <= 0) return top;
    // Round the span up, so the capture covers it.
    const uint64_t span = static_cast<uint64_t>(span_hz);
    uint64_t r = (span + kAudioRateStep - 1) / kAudioRateStep * kAudioRateStep;
    if (r < kMinRate) r = kMinRate;
    if (r > top) r = top;
    return static_cast<uint32_t>(r);
}

} // namespace sdr
