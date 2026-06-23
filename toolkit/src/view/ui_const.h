/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */



#pragma once

#include <cstdint>

namespace view {

// some font constants
constexpr const char* ICON_SIGN_OUT           = "\uE42A";
constexpr const char* ICON_TEXT_BOLD          = "\uE5BE";
constexpr const char* ICON_MOON               = "\uE330";
constexpr const char* ICON_SUN                = "\uE474";
constexpr const char* ICON_SQUARE_ARROW_LEFT  = "\uE074";
constexpr const char* ICON_SQUARE_ARROW_RIGHT = "\uE076";
constexpr const char* ICON_MINUS              = "\uE32A";
constexpr const char* ICON_PLUS               = "\uE3D4";
constexpr const char* ICON_INFO               = "\uE2CE";
// SDR nav icons. Codepoints verified by rendering Phosphor-Fill directly
// (the bundled font's mapping doesn't match every Phosphor release).
constexpr const char* ICON_TUNE_DOWN          = ICON_MINUS;
constexpr const char* ICON_TUNE_UP            = ICON_PLUS;
constexpr const char* ICON_CARET_LEFT         = "\uE138"; // "<"
constexpr const char* ICON_CARET_RIGHT        = "\uE13A"; // ">"
constexpr const char* ICON_CARET_UP           = "\uE13C"; // "^" (list up)
constexpr const char* ICON_CARET_DOWN         = "\uE136"; // "v" (list down)
constexpr const char* ICON_CHECK              = "\uE184"; // check-circle (select)
constexpr const char* ICON_CHART_LINE         = "\uE154"; // line trace (trails)
constexpr const char* ICON_BROADCAST          = "\uE0F2"; // concentric signal
constexpr const char* ICON_MODE               = "\uEA9A"; // wave-sine, demod mode
constexpr const char* ICON_BAND               = "\uE77E"; // radio, band switch
constexpr const char* ICON_KEYBOARD           = "\uE2D8"; // frequency entry
constexpr const char* ICON_GRID_FREQ          = "\uE546"; // columns (vertical lines)
constexpr const char* ICON_GRID_TIME          = "\uE5A2"; // rows (horizontal lines)
constexpr const char* ICON_MAP_TOGGLE         = "\uE3E6"; // squares-2x2, radar<->map view (provisional)
constexpr const char* ICON_PEAK               = "\uE802"; // waveform, peak hold
constexpr const char* ICON_SPEAKER            = "\uE44A"; // speaker-high, audio on
constexpr const char* ICON_SPEAKER_MUTE       = "\uE458"; // speaker-x, muted

namespace color {

// Naive UI common color tokens, flattened to solid RGB values for LVGL.
namespace light {
constexpr uint32_t kPrimary      = 0x18a058;
constexpr uint32_t kInfo         = 0x2080f0;
constexpr uint32_t kBody         = 0xffffff;
constexpr uint32_t kCard         = 0xffffff;
constexpr uint32_t kAction       = 0xfafafc;
constexpr uint32_t kButton       = 0xf5f5f5;
constexpr uint32_t kBorder       = 0xe0e0e6;
constexpr uint32_t kTextPrimary  = 0x1f2225;
constexpr uint32_t kTextDisabled = 0xc2c2c2;
} // namespace light

namespace dark {
constexpr uint32_t kPrimary      = 0x63e2b7;
constexpr uint32_t kInfo         = 0x70c0e8;
constexpr uint32_t kBody         = 0x101014;
constexpr uint32_t kCard         = 0x18181c;
constexpr uint32_t kAction       = 0x0f0f0f;
constexpr uint32_t kButton       = 0x141414;
constexpr uint32_t kBorder       = 0x3d3d3d;
constexpr uint32_t kTextPrimary  = 0xe6e6e6;
constexpr uint32_t kTextDisabled = 0x616161;
} // namespace dark

} // namespace color

} // namespace view
