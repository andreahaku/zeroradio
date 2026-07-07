/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace view {

// Software raster primitives for the RGB565 PPI/radar buffers shared by the
// ADS-B and AIS scopes (and any future geo-radar). All write directly into a
// row-major `buf` of `w`x`h` uint16_t pixels and clip to those bounds; out-of-
// range pixels are skipped, never wrapped. Extracted verbatim from the
// per-app copies so the two scopes render identically from one source.

// Filled disc of radius `r` px centred at (cx, cy).
void plot_disc(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t color);

// Thin ring (midpoint circle) of radius `r` around (cx, cy).
void plot_ring(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t color);

// Filled triangle (small markers) by bounding-box + half-plane test.
void plot_triangle(uint16_t* buf, int w, int h, int x0, int y0, int x1, int y1,
                   int x2, int y2, uint16_t color);

// Straight line (Bresenham) from (x0, y0) to (x1, y1).
void plot_line(uint16_t* buf, int w, int h, int x0, int y0, int x1, int y1, uint16_t color);

// Map a normalized magnitude [0,1] to an RGB565 waterfall colour:
// black -> blue -> cyan -> yellow -> red. Shared by the SDR and Survey
// waterfalls; input is clamped to [0,1].
uint16_t colormap_rgb565(float v);

} // namespace view
