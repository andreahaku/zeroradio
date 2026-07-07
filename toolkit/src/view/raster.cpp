/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "raster.h"

#include "lvgl.h"

#include <algorithm>
#include <cassert>
#include <cstdlib> // std::abs(int) used by plot_line
#include <cstring> // std::memmove used by push_waterfall_row

namespace view {

void plot_disc(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || x >= w || y < 0 || y >= h) continue;
            buf[y * w + x] = color;
        }
    }
}

void plot_ring(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t color) {
    int x = r;
    int y = 0;
    int err = 1 - r;
    const auto put = [&](int px, int py) {
        if (px >= 0 && px < w && py >= 0 && py < h) buf[py * w + px] = color;
    };
    while (x >= y) {
        put(cx + x, cy + y); put(cx - x, cy + y);
        put(cx + x, cy - y); put(cx - x, cy - y);
        put(cx + y, cy + x); put(cx - y, cy + x);
        put(cx + y, cy - x); put(cx - y, cy - x);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

void plot_triangle(uint16_t* buf, int w, int h, int x0, int y0, int x1, int y1,
                   int x2, int y2, uint16_t color) {
    const int minx = std::max(0, std::min({x0, x1, x2}));
    const int maxx = std::min(w - 1, std::max({x0, x1, x2}));
    const int miny = std::max(0, std::min({y0, y1, y2}));
    const int maxy = std::min(h - 1, std::max({y0, y1, y2}));
    const auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            const int w0 = edge(x1, y1, x2, y2, x, y);
            const int w1 = edge(x2, y2, x0, y0, x, y);
            const int w2 = edge(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                buf[y * w + x] = color;
            }
        }
    }
}

void plot_line(uint16_t* buf, int w, int h, int x0, int y0, int x1, int y1, uint16_t color) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) buf[y0 * w + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

uint16_t colormap_rgb565(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    float r, g, b;
    if (v < 0.25f) {            // black -> blue
        const float t = v / 0.25f;
        r = 0.0f; g = 0.0f; b = t;
    } else if (v < 0.5f) {      // blue -> cyan
        const float t = (v - 0.25f) / 0.25f;
        r = 0.0f; g = t; b = 1.0f;
    } else if (v < 0.75f) {     // cyan -> yellow
        const float t = (v - 0.5f) / 0.25f;
        r = t; g = 1.0f; b = 1.0f - t;
    } else {                    // yellow -> red
        const float t = (v - 0.75f) / 0.25f;
        r = 1.0f; g = 1.0f - t; b = 0.0f;
    }

    const auto to_u8 = [](float c) -> uint8_t {
        const int x = static_cast<int>(c * 255.0f + 0.5f);
        return static_cast<uint8_t>(std::clamp(x, 0, 255));
    };
    return lv_color_to_u16(lv_color_make(to_u8(r), to_u8(g), to_u8(b)));
}

void push_waterfall_row(uint16_t* buf, int w, int h, const float* mags) {
    // Caller contract (see raster.h): w >= 1, h >= 1 — h == 0 would underflow
    // the memmove size below.
    assert(buf && mags && w >= 1 && h >= 1);

    // Scroll everything down by one row (new data at the top, flowing
    // downward), then colormap the new top row. Extracted verbatim from the
    // SDR and Survey waterfalls.
    std::memmove(buf + w, buf, static_cast<size_t>(w) * (h - 1) * sizeof(uint16_t));
    for (int x = 0; x < w; ++x) {
        buf[x] = colormap_rgb565(mags[x]);
    }
}

} // namespace view
