/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace sdr {

// A column of crop_to_columns() with no data under it.
constexpr float kBlankColumn = -1e30f;

// Average the fftshifted FFT `db` (`fft_size` bins over `rate_hz`) into
// `n_cols` columns covering `span_hz` around the centre. When the span is
// wider than the rate, the columns past the captured band get kBlankColumn,
// so every column keeps its place on the frequency scale.
void crop_to_columns(const float* db, int fft_size, double span_hz, double rate_hz, float* out,
                     int n_cols);

} // namespace sdr
