/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Shared private helpers for the AisScreen translation units
// (ais_screen*.cpp). Not part of the app's public surface.

#include "lvgl.h"

#include <cstdint>

namespace ais {
namespace common {

constexpr int32_t kPpiSize = 120;
constexpr int32_t kPpiMercW = 320;
constexpr int32_t kPpiMercH = 120;
constexpr int32_t kDetailRadarSize = 118;

// Short text for an AIS navigation status (ITU-R M.1371).
inline const char* nav_status_text(int s) {
    switch (s) {
        case 0:  return "Under way (engine)";
        case 1:  return "At anchor";
        case 2:  return "Not under command";
        case 3:  return "Restricted manoeuvr.";
        case 4:  return "Constrained draught";
        case 5:  return "Moored";
        case 6:  return "Aground";
        case 7:  return "Fishing";
        case 8:  return "Under way (sailing)";
        case 11: return "Towing astern";
        case 12: return "Pushing ahead";
        case 14: return "AIS-SART";
        default: return "";
    }
}

// RGB encoding the vessel by navigation status (mirrors ADS-B's category colour).
inline uint32_t nav_rgb(int s) {
    switch (s) {
        case 0: case 8:  return 0x6fd66f; // under way: green
        case 1: case 5:  return 0x88a0c0; // anchored / moored: blue-grey
        case 7:          return 0x66ccff; // fishing: cyan
        case 2: case 3:
        case 4: case 6:  return 0xffb347; // impaired / aground: orange
        default:         return 0xc8c8c8; // unknown/other: grey
    }
}

inline lv_color_t nav_color(int s) { return lv_color_hex(nav_rgb(s)); }

inline double to_unit(double nm, bool km) { return km ? nm * 1.852 : nm; }
inline const char* dist_unit(bool km) { return km ? "km" : "NM"; }

} // namespace common
} // namespace ais
