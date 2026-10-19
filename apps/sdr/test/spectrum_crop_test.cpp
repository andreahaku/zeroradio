/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// sdr::crop_to_columns(): the zoom crop from the FFT to the screen columns.
// A zoom wider than the sample rate keeps the frequency scale and leaves the
// edges blank, instead of stretching the captured band over the screen.

#include "spectrum_crop.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

constexpr int kFft = 8192;
constexpr int kCols = 320;

// An FFT with one strong bin `offset_hz` from the centre.
std::vector<float> tone(double rate_hz, double offset_hz) {
    std::vector<float> db(kFft, -100.0f);
    const int bin = kFft / 2 + static_cast<int>(offset_hz / rate_hz * kFft);
    db[static_cast<size_t>(bin)] = 0.0f;
    return db;
}

// The column holding the strongest value.
int peak_column(const std::vector<float>& cols) {
    int best = 0;
    for (int j = 1; j < kCols; ++j) {
        if (cols[static_cast<size_t>(j)] > cols[static_cast<size_t>(best)]) best = j;
    }
    return best;
}

int blank_columns(const std::vector<float>& cols) {
    int n = 0;
    for (float v : cols) n += v == sdr::kBlankColumn ? 1 : 0;
    return n;
}

} // namespace

int main() {
    std::vector<float> cols(kCols);

    // Narrow zoom: 100 kHz out of 960 kHz, no blank column, and a tone 25 kHz
    // up sits three quarters across.
    auto db = tone(960000.0, 25000.0);
    sdr::crop_to_columns(db.data(), kFft, 100000.0, 960000.0, cols.data(), kCols);
    check(blank_columns(cols) == 0, "narrow zoom: no blank column");
    check(peak_column(cols) == 240, "narrow zoom: tone at 3/4 of the screen");

    // 1 MHz zoom out of 960 kHz: 6 blank columns on each side, and a tone
    // 250 kHz up still sits three quarters across (the scale holds).
    db = tone(960000.0, 250000.0);
    sdr::crop_to_columns(db.data(), kFft, 1000000.0, 960000.0, cols.data(), kCols);
    check(blank_columns(cols) == 12, "1 MHz zoom: 40 kHz blank");
    check(cols[0] == sdr::kBlankColumn && cols[kCols - 1] == sdr::kBlankColumn,
          "1 MHz zoom: the blank columns are the edges");
    check(cols[7] != sdr::kBlankColumn && cols[kCols - 8] != sdr::kBlankColumn,
          "1 MHz zoom: the captured band starts after the edge");
    check(peak_column(cols) == 240, "1 MHz zoom: tone at 3/4 of the screen");

    // 2 MHz zoom out of 960 kHz: only the middle 48% has data.
    db = tone(960000.0, -400000.0);
    sdr::crop_to_columns(db.data(), kFft, 2000000.0, 960000.0, cols.data(), kCols);
    const int blank = blank_columns(cols);
    check(blank >= 164 && blank <= 168, "2 MHz zoom: about 52% blank");
    check(peak_column(cols) == 96, "2 MHz zoom: tone 400 kHz down at column 96");

    // Same rate as span: every column has data.
    db = tone(2400000.0, 0.0);
    sdr::crop_to_columns(db.data(), kFft, 2400000.0, 2400000.0, cols.data(), kCols);
    check(blank_columns(cols) == 0, "span equal to the rate: no blank column");
    check(peak_column(cols) == 160, "centre tone in the centre column");

    if (failures == 0) std::printf("spectrum_crop_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
