/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"

#include "ui_const.h"

namespace view {

lv_color_t readable(uint32_t rgb, bool dark_mode) {
    if (dark_mode) return lv_color_hex(rgb);
    // 55 % of each channel: the pastel accents keep their hue but get the
    // contrast of a dark text on the light background.
    const auto scale = [rgb](int shift) { return ((rgb >> shift) & 0xff) * 55 / 100; };
    return lv_color_make(static_cast<uint8_t>(scale(16)), static_cast<uint8_t>(scale(8)),
                         static_cast<uint8_t>(scale(0)));
}

ThemePalette palette(bool dark_mode) {
    if (dark_mode) {
        return {
            lv_color_hex(color::dark::kBody),
            lv_color_hex(color::dark::kCard),
            lv_color_hex(color::dark::kAction),
            lv_color_hex(color::dark::kButton),
            lv_color_hex(color::dark::kBorder),
            lv_color_hex(color::dark::kTextPrimary),
            lv_color_hex(color::dark::kTextDisabled),
            lv_color_hex(color::dark::kPrimary),
            lv_color_hex(color::dark::kInfo),
        };
    }

    return {
        lv_color_hex(color::light::kBody),
        lv_color_hex(color::light::kCard),
        lv_color_hex(color::light::kAction),
        lv_color_hex(color::light::kButton),
        lv_color_hex(color::light::kBorder),
        lv_color_hex(color::light::kTextPrimary),
        lv_color_hex(color::light::kTextDisabled),
        lv_color_hex(color::light::kPrimary),
        lv_color_hex(color::light::kInfo),
    };
}

void apply_lvgl_theme(lv_display_t* display, bool dark_mode) {
    if (!display) {
        return;
    }

    const auto colors = palette(dark_mode);
    lv_theme_t* theme = lv_theme_default_init(display, colors.primary, colors.info, dark_mode, LV_FONT_DEFAULT);
    lv_display_set_theme(display, theme);
}

} // namespace view
