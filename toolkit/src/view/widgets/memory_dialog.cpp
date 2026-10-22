/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "memory_dialog.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

namespace toolkit {

namespace {

// Trim spaces at both ends: a stray space must not make a different name.
std::string trimmed(const std::string& s) {
    const auto first = s.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

} // namespace

MemoryDialog::MemoryDialog(app::AssetManager& assets, bool dark_mode, const std::string& title,
                           const std::string& info, std::string name, std::string group,
                           OnSave on_save)
    : on_save_(std::move(on_save)), dark_(dark_mode) {
    fields_[0].text = std::move(name);
    fields_[0].max = kMaxName;
    fields_[1].text = std::move(group);
    fields_[1].max = kMaxGroup;

    const auto pal = view::palette(dark_);
    auto* title_font = assets.load_font("inter-semibold.ttf", 14);
    auto* text_font = assets.load_font("inter-regular.ttf", 12);
    const lv_font_t* fallback = &lv_font_montserrat_12;

    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(root_, pal.background, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 6, 0);
    lv_obj_set_style_pad_row(root_, 3, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);

    auto* head = lv_label_create(root_);
    lv_label_set_text(head, title.c_str());
    lv_obj_set_style_text_font(head, title_font ? title_font : fallback, 0);
    lv_obj_set_style_text_color(head, pal.primary, 0);

    auto* channel = lv_label_create(root_);
    lv_label_set_text(channel, info.c_str());
    lv_obj_set_style_text_font(channel, title_font ? title_font : fallback, 0);
    lv_obj_set_style_text_color(channel, pal.text, 0);

    const char* captions[2] = {"Name", "Group"};
    for (int i = 0; i < 2; ++i) {
        auto* caption = lv_label_create(root_);
        lv_label_set_text(caption, captions[i]);
        lv_obj_set_style_text_font(caption, text_font ? text_font : fallback, 0);
        lv_obj_set_style_text_color(caption, pal.text_disabled, 0);

        auto* label = lv_label_create(root_);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, title_font ? title_font : fallback, 0);
        lv_obj_set_style_pad_hor(label, 3, 0);
        lv_obj_set_style_radius(label, 3, 0);
        fields_[i].label = label;
    }

    auto* hint = lv_label_create(root_);
    lv_label_set_text(hint, "Enter next/save   Tab switch   Esc cancel");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_font(hint, text_font ? text_font : fallback, 0);
    lv_obj_set_style_text_color(hint, pal.text_disabled, 0);

    render();
    platform::set_key_capture(key_cb, this, /*text=*/true);
}

MemoryDialog::~MemoryDialog() {
    close();
}

void MemoryDialog::key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MemoryDialog*>(ctx)) self->on_key(key);
}

void MemoryDialog::on_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        close();
        return;
    }
    if (key == LV_KEY_ENTER) {
        if (active_ == 0) {
            active_ = 1;
            render();
            return;
        }
        const std::string name = trimmed(fields_[0].text);
        const std::string group = trimmed(fields_[1].text);
        if (name.empty()) { // a memory needs a name: go back to it
            active_ = 0;
            render();
            return;
        }
        auto on_save = std::move(on_save_);
        close(); // before the callback, which may open another overlay
        if (on_save) on_save(name, group);
        return;
    }
    if (key == LV_KEY_NEXT || key == LV_KEY_UP || key == LV_KEY_DOWN) {
        active_ = 1 - active_;
        render();
        return;
    }
    Field& f = fields_[active_];
    if (key == LV_KEY_BACKSPACE) {
        f.fresh = false;
        if (!f.text.empty()) f.text.pop_back();
    } else if (key >= 0x20 && key < 0x7f) {
        if (f.fresh) {
            f.text.clear();
            f.fresh = false;
        }
        if (f.text.size() < f.max) f.text.push_back(static_cast<char>(key));
    } else {
        return;
    }
    render();
}

void MemoryDialog::render() {
    if (!root_) return;
    const auto pal = view::palette(dark_);
    for (int i = 0; i < 2; ++i) {
        const bool on = i == active_;
        const std::string shown = fields_[i].text + (on ? "_" : "");
        lv_label_set_text(fields_[i].label, shown.c_str());
        lv_obj_set_style_bg_opa(fields_[i].label, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(fields_[i].label, pal.primary, 0);
        lv_obj_set_style_text_color(fields_[i].label, on ? lv_color_black() : pal.text, 0);
    }
}

void MemoryDialog::close() {
    if (!root_) return;
    platform::set_key_capture(nullptr, nullptr);
    lv_obj_delete(root_);
    root_ = nullptr;
    fields_[0].label = nullptr;
    fields_[1].label = nullptr;
}

} // namespace toolkit
