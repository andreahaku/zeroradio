/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ism_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace ism {
namespace {

// List columns fit the 320px content width: MODEL | TYPE | AGE.
constexpr int32_t kColW[3] = {170, 90, 60};
const char* const kColTitle[3] = {"MODEL", "TYPE", "AGE"};
constexpr int32_t kListHeaderH = 15;

std::string field_of(const toolkit::Entity& e, const char* key) {
    const auto it = e.fields.find(key);
    return it != e.fields.end() ? it->second : std::string{};
}

// "12s" / "3m" / "1h" — compact age like the ADS-B AGE column.
std::string age_text(double seconds) {
    if (seconds < 60.0) return std::to_string(static_cast<int>(seconds)) + "s";
    if (seconds < 3600.0) return std::to_string(static_cast<int>(seconds / 60.0)) + "m";
    return std::to_string(static_cast<int>(seconds / 3600.0)) + "h";
}

} // namespace

IsmScreen::IsmScreen(IsmViewModel& vm, app::AssetManager& assets, toolkit::EntityStore& store,
                     std::function<bool()> source_state)
    : screen::BaseScreen(vm, vm, assets),
      vm_(vm),
      store_(store),
      source_state_(std::move(source_state)) {
    init();
}

IsmScreen::~IsmScreen() {
    if (timer_) lv_timer_delete(timer_);
}

void IsmScreen::build_content(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 0, 0);

    font_small_ = assets().load_font("inter-regular.ttf", 12);
    font_mono_  = assets().load_font("inter-semibold.ttf", 12);
    font_bold_  = assets().load_font("inter-bold.ttf", 12);

    // --- Header: count | title | source dot ---
    header_ = lv_obj_create(content);
    lv_obj_remove_style_all(header_);
    lv_obj_set_size(header_, LV_PCT(100), kHeaderHeight);
    lv_obj_clear_flag(header_, LV_OBJ_FLAG_SCROLLABLE);

    header_count_ = lv_label_create(header_);
    lv_label_set_text(header_count_, "");
    lv_obj_set_style_text_font(header_count_, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(header_count_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(header_count_, LV_ALIGN_LEFT_MID, 4, 0);

    header_title_ = lv_label_create(header_);
    lv_label_set_text(header_title_, "ISM");
    lv_obj_set_style_text_font(header_title_, font_mono_ ? font_mono_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(header_title_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(header_title_, LV_ALIGN_CENTER, 0, 0);

    conn_dot_ = lv_obj_create(header_);
    lv_obj_remove_style_all(conn_dot_);
    lv_obj_set_size(conn_dot_, 8, 8);
    lv_obj_set_style_radius(conn_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(conn_dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(conn_dot_, lv_color_hex(0x888888), 0);
    lv_obj_clear_flag(conn_dot_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(conn_dot_, LV_ALIGN_RIGHT_MID, -6, 0);

    battery_ = std::make_unique<view::widgets::BatteryBadge>(header_);
    lv_obj_align(battery_->obj(), LV_ALIGN_RIGHT_MID, -20, 0);

    // --- Body: the three interchangeable views ---
    body_ = lv_obj_create(content);
    lv_obj_remove_style_all(body_);
    lv_obj_set_width(body_, LV_PCT(100));
    lv_obj_set_flex_grow(body_, 1);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(body_, 0, 0);

    // List view: fixed column-header strip + scrolling 3-column table.
    list_view_ = lv_obj_create(body_);
    lv_obj_remove_style_all(list_view_);
    lv_obj_set_size(list_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(list_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(list_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(list_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_view_, 0, 0);

    auto* list_header = lv_obj_create(list_view_);
    lv_obj_remove_style_all(list_header);
    lv_obj_set_size(list_header, LV_PCT(100), kListHeaderH);
    lv_obj_clear_flag(list_header, LV_OBJ_FLAG_SCROLLABLE);
    int32_t x = 8;
    for (int c = 0; c < 3; ++c) {
        auto* lbl = lv_label_create(list_header);
        lv_label_set_text(lbl, kColTitle[c]);
        lv_obj_set_style_text_font(lbl, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x808080), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, x, 0);
        x += kColW[c];
    }

    list_table_ = lv_table_create(list_view_);
    lv_obj_set_width(list_table_, LV_PCT(100));
    lv_obj_set_flex_grow(list_table_, 1);
    lv_table_set_column_count(list_table_, 3);
    for (int c = 0; c < 3; ++c) lv_table_set_column_width(list_table_, c, kColW[c]);
    lv_obj_set_style_pad_ver(list_table_, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(list_table_, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_width(list_table_, 0, 0);
    if (font_mono_) lv_obj_set_style_text_font(list_table_, font_mono_, LV_PART_ITEMS);
    lv_obj_remove_flag(list_table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(list_table_, list_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(list_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    // Detail view: a single multi-line label (field grid).
    detail_box_ = lv_obj_create(body_);
    lv_obj_remove_style_all(detail_box_);
    lv_obj_set_size(detail_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(detail_box_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(detail_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(detail_box_, 6, 0);

    detail_label_ = lv_label_create(detail_box_);
    lv_label_set_text(detail_label_, "");
    lv_obj_set_style_text_font(detail_label_, font_mono_ ? font_mono_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(detail_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(detail_label_, LV_ALIGN_TOP_LEFT, 0, 0);

    // Settings view: a 2-column name|value table, same look as the List.
    settings_box_ = lv_obj_create(body_);
    lv_obj_remove_style_all(settings_box_);
    lv_obj_set_size(settings_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(settings_box_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(settings_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(settings_box_, 0, 0);

    settings_table_ = lv_table_create(settings_box_);
    lv_obj_set_size(settings_table_, LV_PCT(100), LV_PCT(100));
    lv_table_set_column_count(settings_table_, 2);
    lv_table_set_column_width(settings_table_, 0, 150);
    lv_table_set_column_width(settings_table_, 1, 158);
    lv_obj_set_style_pad_ver(settings_table_, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(settings_table_, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_width(settings_table_, 0, 0);
    if (font_mono_) lv_obj_set_style_text_font(settings_table_, font_mono_, LV_PART_ITEMS);
    lv_obj_remove_flag(settings_table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings_table_, settings_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(settings_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    show_view(vm_.screen());
    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);
}

void IsmScreen::show_view(int screen) {
    const auto s = static_cast<IsmViewModel::Screen>(screen);
    lv_obj_add_flag(list_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(detail_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_box_, LV_OBJ_FLAG_HIDDEN);
    if (s == IsmViewModel::Screen::List) lv_obj_remove_flag(list_view_, LV_OBJ_FLAG_HIDDEN);
    else if (s == IsmViewModel::Screen::Detail) lv_obj_remove_flag(detail_box_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(settings_box_, LV_OBJ_FLAG_HIDDEN);
}

int IsmScreen::row_of(const std::vector<Row>& rows, const std::string& key) const {
    if (key.empty()) return -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].key == key) return static_cast<int>(i);
    return -1;
}

std::vector<IsmScreen::Row> IsmScreen::build_rows(
    const std::vector<toolkit::Entity>& snap) const {
    const auto now = std::chrono::steady_clock::now();
    std::vector<Row> rows;
    rows.reserve(snap.size());
    for (const auto& e : snap) {
        Row r;
        r.key = e.id;
        r.model = field_of(e, "model");
        r.type = field_of(e, "type");
        r.age = std::chrono::duration<double>(now - e.updated).count();
        const std::string rssi = field_of(e, "rssi");
        if (!rssi.empty()) {
            r.has_rssi = true;
            r.rssi = std::strtod(rssi.c_str(), nullptr);
        }
        r.entity = &e;
        rows.push_back(std::move(r));
    }

    switch (static_cast<IsmViewModel::Sort>(vm_.sort_mode())) {
        case IsmViewModel::Sort::Model:
            std::sort(rows.begin(), rows.end(),
                      [](const Row& a, const Row& b) { return a.key < b.key; });
            break;
        case IsmViewModel::Sort::Age:
            std::sort(rows.begin(), rows.end(),
                      [](const Row& a, const Row& b) { return a.age < b.age; });
            break;
        case IsmViewModel::Sort::Rssi:
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                if (a.has_rssi != b.has_rssi) return a.has_rssi; // known RSSI first
                return a.rssi > b.rssi;                           // strongest first
            });
            break;
    }
    return rows;
}

void IsmScreen::tick() {
    store_.sweep(vm_.ttl_seconds());
    snapshot_ = store_.snapshot();
    const std::vector<Row> rows = build_rows(snapshot_);

    std::vector<std::string> order;
    order.reserve(rows.size());
    for (const auto& r : rows) order.push_back(r.key);
    vm_.set_visible_order(std::move(order));

    // Source dot: green once decoding, grey until then.
    lv_obj_set_style_bg_color(
        conn_dot_, source_state_ && source_state_() ? lv_color_hex(0x63e2b7) : lv_color_hex(0x888888),
        0);

    char count[24];
    std::snprintf(count, sizeof(count), "%zu dev", rows.size());
    lv_label_set_text(header_count_, count);

    show_view(vm_.screen());
    switch (static_cast<IsmViewModel::Screen>(vm_.screen())) {
        case IsmViewModel::Screen::List: update_list(rows); break;
        case IsmViewModel::Screen::Detail: update_detail(rows); break;
        case IsmViewModel::Screen::Settings: update_settings(); break;
    }
}

void IsmScreen::update_list(const std::vector<Row>& rows) {
    if (!list_table_) return;
    lv_table_set_row_count(list_table_, static_cast<uint32_t>(rows.size()));

    const std::string& sel = vm_.selected_key();
    const auto set_cell = [&](uint32_t rr, uint16_t cc, const char* val) {
        const char* cur = lv_table_get_cell_value(list_table_, rr, cc);
        if (!cur || std::strcmp(cur, val) != 0) lv_table_set_cell_value(list_table_, rr, cc, val);
    };
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const uint32_t row = static_cast<uint32_t>(i);
        const char* mark = (r.key == sel && !sel.empty()) ? "\xE2\x97\x8f" : ""; // ●
        const std::string model = std::string(mark) + (r.model.empty() ? r.key : r.model);
        set_cell(row, 0, model.c_str());
        set_cell(row, 1, r.type.empty() ? "-" : r.type.c_str());
        set_cell(row, 2, age_text(r.age).c_str());
    }

    const int cur = row_of(rows, vm_.cursor_key());
    list_sel_row_ = cur;
    if (cur >= 0) lv_table_set_selected_cell(list_table_, static_cast<uint16_t>(cur), 0);

    const std::string title = vm_.selected_key().empty() ? "ISM" : vm_.selected_key();
    lv_label_set_text(header_title_, title.c_str());
}

void IsmScreen::update_detail(const std::vector<Row>& rows) {
    // Detail follows the selection; fall back to the cursor when nothing locked.
    std::string key = vm_.selected_key();
    if (key.empty()) key = vm_.cursor_key();
    const int idx = row_of(rows, key);
    if (idx < 0 || !rows[idx].entity) {
        lv_label_set_text(detail_label_, "No device selected.");
        lv_label_set_text(header_title_, "ISM");
        return;
    }
    const toolkit::Entity& e = *rows[idx].entity;
    std::string text;
    const auto line = [&](const char* label, const std::string& v) {
        if (!v.empty()) text += std::string(label) + ": " + v + "\n";
    };
    line("MODEL", field_of(e, "model"));
    line("ID", field_of(e, "id"));
    line("CHAN", field_of(e, "channel"));
    line("TYPE", field_of(e, "type"));
    line("TEMP", field_of(e, "temp").empty() ? "" : field_of(e, "temp") + " C");
    line("HUM", field_of(e, "hum").empty() ? "" : field_of(e, "hum") + " %");
    line("PRESS", field_of(e, "press").empty() ? "" : field_of(e, "press") + " kPa");
    line("BATT", field_of(e, "batt"));
    line("RSSI", field_of(e, "rssi").empty() ? "" : field_of(e, "rssi") + " dB");
    line("SNR", field_of(e, "snr").empty() ? "" : field_of(e, "snr") + " dB");
    line("FREQ", field_of(e, "freq").empty() ? "" : field_of(e, "freq") + " MHz");
    line("HEARD", age_text(rows[idx].age) + " ago");
    // Extras (x:*), listed after the mapped fields.
    for (const auto& [k, v] : e.fields) {
        if (k.rfind("x:", 0) == 0) text += "  " + k.substr(2) + ": " + v + "\n";
    }
    if (!text.empty() && text.back() == '\n') text.pop_back();
    lv_label_set_text(detail_label_, text.c_str());
    lv_label_set_text(header_title_, rows[idx].model.empty() ? "ISM" : rows[idx].model.c_str());
}

void IsmScreen::update_settings() {
    if (!settings_table_) return;
    const int n = vm_.settings_count();
    lv_table_set_row_count(settings_table_, static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) {
        lv_table_set_cell_value(settings_table_, static_cast<uint16_t>(i), 0,
                                vm_.setting_name(i).c_str());
        lv_table_set_cell_value(settings_table_, static_cast<uint16_t>(i), 1,
                                vm_.setting_value(i).c_str());
    }
    settings_sel_row_ = vm_.settings_cursor();
    lv_table_set_selected_cell(settings_table_, static_cast<uint16_t>(settings_sel_row_), 0);
    lv_label_set_text(header_title_, "Settings");
}

void IsmScreen::list_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<IsmScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) return;
    const bool is_sel = (static_cast<int>(base->id1) == self->list_sel_row_);
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

void IsmScreen::settings_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<IsmScreen*>(lv_event_get_user_data(event));
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

void IsmScreen::tick_cb(lv_timer_t* timer) {
    if (auto* self = static_cast<IsmScreen*>(lv_timer_get_user_data(timer))) self->tick();
}

} // namespace ism
