/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "spectrum_crop.h"

namespace sdr {

void crop_to_columns(const float* db, int fft_size, double span_hz, double rate_hz, float* out,
                     int n_cols) {
    if (!db || !out || fft_size <= 0 || n_cols <= 0 || rate_hz <= 0.0) return;
    double frac = span_hz / rate_hz;
    if (frac < 1.0 / fft_size) frac = 1.0 / fft_size;
    const double visible = frac * fft_size;
    const double start = (fft_size - visible) * 0.5; // negative when wider than the FFT
    for (int j = 0; j < n_cols; ++j) {
        const double lo_d = start + visible * j / n_cols;
        const double hi_d = start + visible * (j + 1) / n_cols;
        if (hi_d <= 0.0 || lo_d >= fft_size) {
            out[j] = kBlankColumn;
            continue;
        }
        int lo = lo_d < 0.0 ? 0 : static_cast<int>(lo_d);
        int hi = static_cast<int>(hi_d);
        if (hi <= lo) hi = lo + 1;
        if (hi > fft_size) hi = fft_size;
        float sum = 0.0f;
        for (int k = lo; k < hi; ++k) sum += db[k];
        out[j] = sum / static_cast<float>(hi - lo);
    }
}

} // namespace sdr
