/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "memories_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

namespace memories {
namespace {

constexpr int32_t kColumnWidths[3] = {165, 105, 50};
constexpr const char* kColumnTitles[3] = {"NAME", "FREQ", "MODE"};

} // namespace

MemoriesScreen::MemoriesScreen(MemoriesViewModel& vm, app::AssetManager& assets)
    : BaseScreen(vm, vm, assets), vm_(vm) {
    init();
}

MemoriesScreen::~MemoriesScreen() {
    if (search_dialog_) close_search(false);
    if (scan_dialog_) close_scan(false);
    if (search_observer_) lv_observer_remove(search_observer_);
    if (scan_observer_) lv_observer_remove(scan_observer_);
    if (selection_observer_) lv_observer_remove(selection_observer_);
    if (rows_observer_) lv_observer_remove(rows_observer_);
}

void MemoriesScreen::build_content(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 0, 0);

    auto* small_font = assets().load_font("inter-regular.ttf", 12);
    auto* row_font = assets().load_font("inter-semibold.ttf", 12);

    auto* status = lv_label_create(content);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_height(status, 19);
    lv_obj_set_style_pad_left(status, 4, 0);
    lv_obj_set_style_text_font(status, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(status, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_label_bind_text(status, vm_.status_subject(), nullptr);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);

    auto* header = lv_obj_create(content);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 17);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    int32_t x = 0;
    for (int column = 0; column < 3; ++column) {
        auto* label = lv_label_create(header);
        lv_label_set_text(label, kColumnTitles[column]);
        lv_obj_set_style_text_font(label, small_font ? small_font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x888888), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, x + 4, 0);
        x += kColumnWidths[column];
    }

    table_ = lv_table_create(content);
    reactive::bind_theme(table_, vm_.dark_mode_subject(), reactive::ThemeRole::Table);
    lv_obj_set_width(table_, LV_PCT(100));
    lv_obj_set_flex_grow(table_, 1);
    lv_table_set_column_count(table_, 3);
    for (int column = 0; column < 3; ++column)
        lv_table_set_column_width(table_, column, kColumnWidths[column]);
    lv_obj_set_style_pad_all(table_, 3, LV_PART_ITEMS);
    lv_obj_set_style_border_width(table_, 0, 0);
    lv_obj_set_style_text_font(table_, row_font ? row_font : &lv_font_montserrat_12,
                               LV_PART_ITEMS);
    lv_obj_remove_flag(table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(table_, draw_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    empty_label_ = lv_label_create(content);
    lv_label_set_text(empty_label_, "No memories yet. Press M in the SDR or the Scanner to save one.");
    lv_obj_set_width(empty_label_, LV_PCT(100));
    lv_obj_set_style_pad_all(empty_label_, 8, 0);
    lv_obj_set_style_text_font(empty_label_, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(empty_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);

    rows_observer_ = lv_subject_add_observer(vm_.rows_version_subject(), rows_cb, this);
    selection_observer_ = lv_subject_add_observer(vm_.selected_row_subject(), rows_cb, this);
    search_observer_ = lv_subject_add_observer(vm_.search_request_subject(), search_cb, this);
    scan_observer_ = lv_subject_add_observer(vm_.scan_request_subject(), scan_cb, this);
    update_table();
}

void MemoriesScreen::rows_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<MemoriesScreen*>(lv_observer_get_user_data(observer));
    if (self) self->update_table();
}

void MemoriesScreen::search_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MemoriesScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int request = lv_subject_get_int(subject);
    if (request != self->last_search_request_) {
        self->last_search_request_ = request;
        self->open_search();
    }
}

void MemoriesScreen::scan_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MemoriesScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int request = lv_subject_get_int(subject);
    if (request != self->last_scan_request_) {
        self->last_scan_request_ = request;
        self->open_scan();
    }
}

void MemoriesScreen::draw_cb(lv_event_t* event) {
    auto* self = static_cast<MemoriesScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base || base->part != LV_PART_ITEMS) return;
    const size_t row = static_cast<size_t>(base->id1);
    const auto& rows = self->vm_.rows();
    if (row >= rows.size()) return;
    const bool group = rows[row].group;
    const bool selected = static_cast<int>(row) == self->vm_.selected_row();
    const auto type = lv_draw_task_get_type(task);
    if (type == LV_DRAW_TASK_TYPE_FILL && (group || selected)) {
        auto* fill = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fill->color = group ? view::palette(self->vm_.is_dark_mode()).background
                            : view::palette(self->vm_.is_dark_mode()).primary;
        fill->opa = LV_OPA_COVER;
    } else if (type == LV_DRAW_TASK_TYPE_LABEL) {
        auto* label = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        if (selected) label->color = lv_color_black();
        else if (group) label->color = view::palette(self->vm_.is_dark_mode()).primary;
    }
}

void MemoriesScreen::update_table() {
    if (!table_) return;
    const auto& rows = vm_.rows();
    lv_table_set_row_count(table_, static_cast<uint32_t>(rows.size()));
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto row = static_cast<uint32_t>(i);
        const std::string name = rows[i].group ? "[" + rows[i].name + "]"
                                               : (rows[i].marked ? "* " : "  ") + rows[i].name;
        lv_table_set_cell_value(table_, row, 0, name.c_str());
        lv_table_set_cell_value(table_, row, 1, rows[i].frequency.c_str());
        lv_table_set_cell_value(table_, row, 2, rows[i].mode.c_str());
    }
    const int selected = vm_.selected_row();
    if (selected >= 0 && selected < static_cast<int>(rows.size()))
        lv_table_set_selected_cell(table_, static_cast<uint16_t>(selected), 0);
    if (rows.empty()) lv_obj_remove_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(table_);
}

void MemoriesScreen::open_search() {
    if (search_dialog_) return;
    search_text_ = vm_.query();
    auto* title_font = assets().load_font("inter-semibold.ttf", 16);
    auto* small_font = assets().load_font("inter-regular.ttf", 12);
    const auto& palette = view::palette(vm_.is_dark_mode());

    search_dialog_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(search_dialog_);
    lv_obj_set_size(search_dialog_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(search_dialog_, palette.background, 0);
    lv_obj_set_style_bg_opa(search_dialog_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(search_dialog_, 8, 0);
    lv_obj_set_style_pad_row(search_dialog_, 10, 0);
    lv_obj_clear_flag(search_dialog_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(search_dialog_, LV_FLEX_FLOW_COLUMN);

    auto* title = lv_label_create(search_dialog_);
    lv_label_set_text(title, "Search memories by name");
    lv_obj_set_style_text_font(title, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, palette.primary, 0);

    search_input_ = lv_label_create(search_dialog_);
    lv_obj_set_width(search_input_, LV_PCT(100));
    lv_obj_set_style_text_font(search_input_, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(search_input_, palette.text, 0);

    auto* hint = lv_label_create(search_dialog_);
    lv_label_set_text(hint, "Type a name  |  Enter: filter  |  Esc: cancel\nClear text to show all memories");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_font(hint, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, palette.text_disabled, 0);

    render_search();
    platform::set_key_capture(search_key_cb, this, /*text=*/true);
}

void MemoriesScreen::close_search(bool apply) {
    platform::set_key_capture(nullptr, nullptr);
    if (search_dialog_) {
        lv_obj_delete(search_dialog_);
        search_dialog_ = nullptr;
        search_input_ = nullptr;
    }
    if (apply) vm_.set_query(search_text_);
}

void MemoriesScreen::search_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MemoriesScreen*>(ctx)) self->on_search_key(key);
}

void MemoriesScreen::on_search_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        close_search(false);
    } else if (key == LV_KEY_ENTER) {
        close_search(true);
    } else if (key == LV_KEY_BACKSPACE) {
        if (!search_text_.empty()) search_text_.pop_back();
        render_search();
    } else if (key >= 0x20 && key < 0x7f && search_text_.size() < 32) {
        search_text_.push_back(static_cast<char>(key));
        render_search();
    }
}

void MemoriesScreen::render_search() {
    if (search_input_)
        lv_label_set_text_fmt(search_input_, "> %s_", search_text_.c_str());
}

void MemoriesScreen::open_scan() {
    if (scan_dialog_ || search_dialog_) return;
    auto* title_font = assets().load_font("inter-semibold.ttf", 16);
    auto* small_font = assets().load_font("inter-regular.ttf", 12);
    const auto& palette = view::palette(vm_.is_dark_mode());

    scan_dialog_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(scan_dialog_);
    lv_obj_set_size(scan_dialog_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(scan_dialog_, palette.background, 0);
    lv_obj_set_style_bg_opa(scan_dialog_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scan_dialog_, 8, 0);
    lv_obj_set_style_pad_row(scan_dialog_, 12, 0);
    lv_obj_clear_flag(scan_dialog_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scan_dialog_, LV_FLEX_FLOW_COLUMN);

    auto* title = lv_label_create(scan_dialog_);
    lv_label_set_text_fmt(title, "Scan %zu %s?", vm_.scan_count(),
                          vm_.scan_count() == 1 ? "memory" : "memories");
    lv_obj_set_style_text_font(title, title_font ? title_font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, palette.primary, 0);

    auto* scope = lv_label_create(scan_dialog_);
    lv_label_set_text(scope, vm_.scan_uses_marks() ? "Marked memories" : "Current group");
    lv_obj_set_style_text_font(scope, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scope, palette.text, 0);

    if (vm_.scan_total() > vm_.scan_count()) {
        auto* limit = lv_label_create(scan_dialog_);
        lv_label_set_text_fmt(limit, "First %zu of %zu (scan limit)", vm_.scan_count(),
                              vm_.scan_total());
        lv_obj_set_style_text_font(limit, small_font ? small_font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(limit, palette.text, 0);
    }

    auto* hint = lv_label_create(scan_dialog_);
    lv_label_set_text(hint, "Enter: start scan  |  Esc: cancel");
    lv_obj_set_style_text_font(hint, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, palette.text_disabled, 0);

    platform::set_key_capture(scan_key_cb, this);
}

void MemoriesScreen::close_scan(bool confirm) {
    platform::set_key_capture(nullptr, nullptr);
    if (scan_dialog_) {
        lv_obj_delete(scan_dialog_);
        scan_dialog_ = nullptr;
    }
    if (confirm) vm_.confirm_scan();
    else vm_.cancel_scan();
}

void MemoriesScreen::scan_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MemoriesScreen*>(ctx)) self->on_scan_key(key);
}

void MemoriesScreen::on_scan_key(uint32_t key) {
    if (key == LV_KEY_ESC) close_scan(false);
    else if (key == LV_KEY_ENTER) close_scan(true);
}

} // namespace memories
