/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */


#include "icon_button.h"

#include "theme.h"

namespace view::widgets {

IconButton::IconButton(lv_obj_t* parent,
                       lv_subject_t* dark_mode_subject,
                       int32_t width,
                       int32_t height,
                       const char* text,
                       const lv_font_t* font,
                       lv_color_t light_color,
                       lv_color_t dark_color,
                       lv_event_cb_t click_cb,
                       void* user_data)
    : BaseWidgets(parent),
      dark_mode_subject_(dark_mode_subject),
      width_(width),
      height_(height),
      text_(text ? text : ""),
      font_(font),
      light_color_(light_color),
      dark_color_(dark_color),
      click_cb_(click_cb),
      user_data_(user_data) {}

bool IconButton::dark_mode() const {
    return dark_mode_subject_ && lv_subject_get_int(dark_mode_subject_) != 0;
}

void IconButton::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_button_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, width_, height_);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(core_obj_, 0, 0);
    lv_obj_set_style_shadow_width(core_obj_, 0, 0);
    lv_obj_set_style_pad_all(core_obj_, 0, 0);

    if (click_cb_) {
        lv_obj_add_event_cb(core_obj_, click_cb_, LV_EVENT_CLICKED, user_data_);
    }

    label_ = lv_label_create(core_obj_);
    lv_label_set_text(label_, text_.c_str());
    if (font_) {
        lv_obj_set_style_text_font(label_, font_, 0);
    }
    lv_obj_center(label_);

    if (dark_mode_subject_) {
        lv_subject_add_observer_obj(dark_mode_subject_, theme_observer_cb, core_obj_, this);
    }
    apply_theme(dark_mode());
}

void IconButton::set_text(const char* text) {
    text_ = text ? text : "";
    if (label_) {
        lv_label_set_text(label_, text_.c_str());
        lv_obj_center(label_);
    }
}

void IconButton::set_font(const lv_font_t* font) {
    font_ = font;
    if (label_ && font_) {
        lv_obj_set_style_text_font(label_, font_, 0);
        lv_obj_center(label_);
    }
}

void IconButton::set_icon_color(lv_color_t color) {
    force_color_ = true;
    forced_color_ = color;
    apply_theme(dark_mode());
}

void IconButton::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!core_obj_) {
        return;
    }

    if (enabled_) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    }
    else {
        lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    }
    apply_theme(dark_mode());
}

void IconButton::theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* button = static_cast<IconButton*>(lv_observer_get_user_data(observer));
    if (!button) {
        return;
    }

    button->apply_theme(lv_subject_get_int(subject) != 0);
}

void IconButton::apply_theme(bool dark_mode) {
    if (!core_obj_ || !label_) {
        return;
    }

    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_TRANSP, 0);

    // Forced colour (overlay NavBar): keep the colour even when disabled, just
    // fade it so the slot stays visible over a busy background (greying it out
    // with the theme's dark "disabled" colour would make it vanish).
    if (force_color_) {
        lv_obj_set_style_text_color(label_, forced_color_, 0);
        lv_obj_set_style_text_opa(label_, enabled_ ? LV_OPA_COVER : LV_OPA_40, 0);
        return;
    }

    if (!enabled_) {
        lv_obj_set_style_text_color(label_, view::palette(dark_mode).text_disabled, 0);
        return;
    }

    lv_obj_set_style_text_color(label_, dark_mode ? dark_color_ : light_color_, 0);
}

} // namespace view::widgets
