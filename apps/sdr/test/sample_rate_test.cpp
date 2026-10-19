/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// sdr::rate_for_span() and sdr::audio_decimation(): every RTL rate the zoom
// can pick must decimate to exactly 48 kHz of audio. A rate that gives more
// fills the 48 kHz output faster than it plays, and the audio drops chunks.

#include "sample_rate.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

// The RTL2832U takes 225-300 kHz or 900 kHz-3.2 MHz.
bool valid_rtl_rate(uint32_t r) {
    return (r >= 225001 && r <= 300000) || (r >= 900001 && r <= 3200000);
}

} // namespace

int main() {
    // The bug this guards: the device's 1.024 Msps cap gave 51.2 kHz audio.
    check(sdr::audio_decimation(1024000.0).audio_hz == 51200.0, "1.024 Msps gives 51.2 kHz");
    check(sdr::audio_decimation(2400000.0).audio_hz == 48000.0, "2.4 Msps gives 48 kHz");

    const int32_t spans[] = {100000, 200000, 500000, 1000000, 1500000, 2000000};
    const uint32_t caps[] = {960000, 1000000, 1024000, 1200000, 2048000, 2400000, 3200000};
    for (uint32_t cap : caps) {
        for (int32_t span : spans) {
            const uint32_t r = sdr::rate_for_span(span, cap);
            const std::string at = std::to_string(span) + " Hz span, cap " + std::to_string(cap);
            check(valid_rtl_rate(r), "valid RTL rate at " + at);
            check(r <= cap, "within the cap at " + at);
            check(sdr::audio_decimation(r).audio_hz == 48000.0, "48 kHz audio at " + at);
            // The span fits in the capture unless the cap is narrower.
            check(r >= static_cast<uint32_t>(span) || r + sdr::kAudioRateStep > cap,
                  "the rate covers the span at " + at);
        }
    }
    check(sdr::rate_for_span(100000, 2400000) == 960000, "narrow zoom: the RTL minimum");
    check(sdr::rate_for_span(1500000, 2400000) == 1680000, "1.5 MHz rounds up to 1.68 Msps");
    check(sdr::rate_for_span(2000000, 1024000) == 960000, "the device cap rounds down");
    check(sdr::rate_for_span(0, 2400000) == 2400000, "no span: the cap");

    // A cap under the 900 kHz floor: the low band's 240 kHz still gives 48 kHz.
    check(sdr::rate_for_span(100000, 250000) == 240000, "low cap: 240 kHz");
    check(sdr::audio_decimation(240000.0).audio_hz == 48000.0, "240 kHz gives 48 kHz");
    check(sdr::rate_for_span(100000, 230000) == 230000, "a cap under 240 kHz is kept");

    if (failures == 0) std::printf("sample_rate_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
