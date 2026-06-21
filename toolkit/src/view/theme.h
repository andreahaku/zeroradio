/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

namespace view {

constexpr int kScreenWidth    = 320;
constexpr int kScreenHeight   = 170;
constexpr int kTitleBarHeight = 30;
constexpr int kNavBarHeight   = 30;

struct ThemePalette {
    lv_color_t background;
    lv_color_t surface;
    lv_color_t bar;
    lv_color_t button;
    lv_color_t border;
    lv_color_t text;
    lv_color_t text_disabled;
    lv_color_t primary;
    lv_color_t info;
};

ThemePalette palette(bool dark_mode);
void apply_lvgl_theme(lv_display_t* display, bool dark_mode);

} // namespace view
