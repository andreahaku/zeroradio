/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_screen.h"

#include "theme.h"
#include "ais_screen_common.h"

namespace ais {

using namespace common;

void AisScreen::settings_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<AisScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) return;
    const bool is_sel = (static_cast<int>(base->id1) == self->settings_sel_row_);
    const lv_draw_task_type_t type = lv_draw_task_get_type(task);
    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel && base->part == LV_PART_ITEMS) {
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(false).primary;
        fd->opa = LV_OPA_COVER;
    } else if (type == LV_DRAW_TASK_TYPE_LABEL && is_sel) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        ld->color = lv_color_black();
    }
}

void AisScreen::update_settings() {
    if (!settings_table_) return;
    const int n = vm_.settings_count();
    lv_table_set_row_count(settings_table_, static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) {
        lv_table_set_cell_value(settings_table_, static_cast<uint32_t>(i), 0,
                                vm_.setting_name(i).c_str());
        lv_table_set_cell_value(settings_table_, static_cast<uint32_t>(i), 1,
                                vm_.setting_value(i).c_str());
    }
    settings_sel_row_ = vm_.settings_cursor();
    if (settings_sel_row_ >= 0) {
        lv_table_set_selected_cell(settings_table_, static_cast<uint16_t>(settings_sel_row_), 0);
    }
}

} // namespace ais
