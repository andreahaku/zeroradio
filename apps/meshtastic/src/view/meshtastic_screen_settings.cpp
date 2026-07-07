/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "linux_input.h"
#include "theme.h"
#include "meshtastic_screen_common.h"

#include <cstdint>
#include <string>
#include <vector>

namespace meshtastic {

using namespace common;

void MeshtasticScreen::settings_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<MeshtasticScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) return;
    const bool is_sel = (static_cast<int>(base->id1) == self->settings_sel_row_);
    const lv_draw_task_type_t type = lv_draw_task_get_type(task);
    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel && base->part == LV_PART_ITEMS) {
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(self->vm_.is_dark_mode()).primary;
        fd->opa = LV_OPA_COVER;
    } else if (type == LV_DRAW_TASK_TYPE_LABEL && is_sel) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        ld->color = lv_color_black();
    }
}

void MeshtasticScreen::update_settings(const std::vector<toolkit::Entity>& snap) {
    if (!settings_table_) return;
    // Feed the primary channel name for the Channel display row.
    for (const auto& c : channels_.active()) {
        if (c.role == 1) {
            vm_.set_settings_channel(c.name.empty() ? "#Primary" : "#" + c.name);
            break;
        }
    }
    const int n = MeshtasticViewModel::kSettingCount;
    lv_table_set_row_count(settings_table_, static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) {
        lv_table_set_cell_value(settings_table_, static_cast<uint32_t>(i), 0,
                                vm_.setting_name(i).c_str());
        lv_table_set_cell_value(settings_table_, static_cast<uint32_t>(i), 1,
                                vm_.setting_value(i).c_str());
    }
    settings_sel_row_ = vm_.settings_cursor();
    lv_obj_invalidate(settings_table_);
}

void MeshtasticScreen::settings_edit_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MeshtasticScreen*>(ctx)) self->on_settings_edit_key(key);
}

void MeshtasticScreen::on_settings_edit_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        vm_.settings_editor_commit();
        close_settings_editor();
    } else if (key == LV_KEY_ESC) {
        vm_.settings_editor_cancel();
        close_settings_editor();
    } else if (key == LV_KEY_BACKSPACE) {
        vm_.settings_editor_backspace();
        if (settings_edit_row_) {
            const std::string s = "> " + vm_.settings_editor_buf() + "_";
            lv_label_set_text(settings_edit_row_, s.c_str());
        }
    } else if (key >= 0x20 && key < 0x7f) {
        vm_.settings_editor_char(static_cast<char>(key));
        if (settings_edit_row_) {
            const std::string s = "> " + vm_.settings_editor_buf() + "_";
            lv_label_set_text(settings_edit_row_, s.c_str());
        }
    }
}

void MeshtasticScreen::open_settings_editor() {
    if (!settings_edit_row_) return;
    const std::string s = "> " + vm_.settings_editor_buf() + "_";
    lv_label_set_text(settings_edit_row_, s.c_str());
    lv_obj_remove_flag(settings_edit_row_, LV_OBJ_FLAG_HIDDEN);
    platform::set_key_capture(settings_edit_key_cb, this);
}

void MeshtasticScreen::close_settings_editor() {
    platform::set_key_capture(nullptr, nullptr);
    if (settings_edit_row_) lv_obj_add_flag(settings_edit_row_, LV_OBJ_FLAG_HIDDEN);
}

} // namespace meshtastic
