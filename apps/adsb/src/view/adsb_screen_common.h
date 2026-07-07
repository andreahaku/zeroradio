/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Shared private helpers for the AdsbScreen translation units
// (adsb_screen*.cpp). Not part of the app's public surface.

#include "lvgl.h"

#include <cstdint>
#include <string>

namespace adsb {
namespace common {

// PPI canvas geometry: a square that fits the body (screen 170 - nav 30 - header
// 18 = ~122px tall), kept 4-byte-stride aligned (120*2 = 240 bytes). A larger
// square would be clipped vertically by the body.
constexpr int32_t kPpiSize = 120;

// Mercator map canvas: not bound to a circle, so it spreads to twice the width
// (same height) to use more of the 320px-wide screen. Shown instead of the
// square PPI when the Radar screen is in map mode (a separate fixed-size canvas
// — resizing one at runtime crashes the SDL/Mesa flush).
constexpr int32_t kPpiMercW = 320; // full screen width; side labels overlay it
constexpr int32_t kPpiMercH = 120;

// Detail mini-radar: a square on the right of the split Detail view.
constexpr int32_t kDetailRadarSize = 118; // 118*2 = 236 bytes (4-byte aligned)

// RGB encoding the aircraft category (emergency overrides to red).
inline uint32_t category_rgb(const std::string& category, bool emergency) {
    if (emergency) return 0xff5050;
    if (category == "light")      return 0x6fd66f; // green
    if (category == "small")      return 0x66ccff; // cyan
    if (category == "large")      return 0x4d9fff; // blue
    if (category == "heavy")      return 0xffb347; // orange
    if (category == "rotorcraft") return 0xc792ea; // purple
    return 0xc8c8c8;                               // other/unknown: grey
}

inline lv_color_t category_color(const std::string& category, bool emergency) {
    return lv_color_hex(category_rgb(category, emergency));
}

// Distance helpers for the units setting (NM default, km optional).
inline double to_unit(double nm, bool km) { return km ? nm * 1.852 : nm; }
inline const char* dist_unit(bool km) { return km ? "km" : "NM"; }

} // namespace common
} // namespace adsb
