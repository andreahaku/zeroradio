/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */


#pragma once

#include "base_widget.h"

#include "lvgl.h"

#include <cstdint>
#include <string>

namespace view::widgets {

class IconButton : public BaseWidgets {

public:
    IconButton(lv_obj_t* parent,
               lv_subject_t* dark_mode_subject,
               int32_t width,
               int32_t height,
               const char* text,
               const lv_font_t* font,
               lv_color_t light_color,
               lv_color_t dark_color,
               lv_event_cb_t click_cb = nullptr,
               void* user_data = nullptr);

    void build() override;
    void set_text(const char* text);
    void set_font(const lv_font_t* font);
    void set_enabled(bool enabled);
    // Force a fixed icon colour, overriding the light/dark theme (used by the
    // overlay NavBar so icons stay legible over a busy background).
    void set_icon_color(lv_color_t color);

private:
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    void apply_theme(bool dark_mode);
    bool dark_mode() const;

    lv_subject_t* dark_mode_subject_;
    int32_t width_;
    int32_t height_;
    std::string text_;
    const lv_font_t* font_;
    lv_color_t light_color_;
    lv_color_t dark_color_;
    lv_event_cb_t click_cb_;
    void* user_data_;
    lv_obj_t* label_{nullptr};
    bool enabled_{true};
    bool force_color_{false};
    lv_color_t forced_color_{};

};

} // namespace view::widgets
