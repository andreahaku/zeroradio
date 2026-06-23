/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "titlebar.h"

#include "bindings.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {

TitleBar::TitleBar(lv_obj_t* parent,
                   lv_subject_t* title_subject,
                   lv_subject_t* subtitle_subject,
                   lv_subject_t* dark_mode_subject)
    : BaseWidgets(parent),
      title_subject_(title_subject),
      subtitle_subject_(subtitle_subject),
      dark_mode_subject_(dark_mode_subject) {}

void TitleBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), view::kTitleBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(core_obj_, dark_mode_subject_, reactive::ThemeRole::Bar);

    auto* title = lv_label_create(core_obj_);
    lv_label_bind_text(title, title_subject_, nullptr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 6, 0);
    reactive::bind_theme(title, dark_mode_subject_, reactive::ThemeRole::Text);

    // Optional subtitle on the right (e.g. status text). Bound to its subject;
    // empty until the app sets it.
    if (subtitle_subject_) {
        auto* subtitle = lv_label_create(core_obj_);
        lv_label_bind_text(subtitle, subtitle_subject_, nullptr);
        lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
        lv_obj_align(subtitle, LV_ALIGN_RIGHT_MID, -6, 0);
        reactive::bind_theme(subtitle, dark_mode_subject_, reactive::ThemeRole::Text);
    }
}

} // namespace view::widgets
