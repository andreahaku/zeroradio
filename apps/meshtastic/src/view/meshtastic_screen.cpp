/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace meshtastic {
namespace {

constexpr uint32_t kTickPeriodMs = 300;
constexpr double kNodeTtlSeconds = 3600.0; // nodes beacon rarely; keep an hour

// NODES table columns (fit the 320px content width).
constexpr int32_t kColW[4] = {116, 64, 54, 62};
const char* const kColTitle[4] = {"SHORT", "SNR", "HOP", "AGE"};
constexpr int32_t kHeaderH = 15;

std::string field_of(const toolkit::Entity& e, const char* key) {
    const auto it = e.fields.find(key);
    return it != e.fields.end() ? it->second : std::string{};
}

} // namespace

MeshtasticScreen::MeshtasticScreen(MeshtasticViewModel& vm,
                                   app::AssetManager& assets,
                                   toolkit::EntityStore& store)
    : BaseScreen(vm, vm, assets), vm_(vm), store_(store) {
    init();
}

MeshtasticScreen::~MeshtasticScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void MeshtasticScreen::build_content(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    font_big_   = assets().load_font("inter-semibold.ttf", 22);
    font_small_ = assets().load_font("inter-regular.ttf", 12);
    const lv_font_t* fb = font_big_ ? font_big_ : &lv_font_montserrat_12;
    const lv_font_t* fs = font_small_ ? font_small_ : &lv_font_montserrat_12;

    // Placeholder big label, centred — shown on every page except NODES.
    view_label_ = lv_label_create(content);
    lv_label_set_text(view_label_, "CHATS");
    lv_obj_set_style_text_font(view_label_, fb, 0);
    reactive::bind_theme(view_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(view_label_, LV_ALIGN_CENTER, 0, -6);

    hint_label_ = lv_label_create(content);
    lv_label_set_text(hint_label_, "press 4 to switch view  -  ESC to quit");
    lv_obj_set_style_text_font(hint_label_, fs, 0);
    lv_obj_set_style_text_color(hint_label_, lv_color_hex(0x888888), 0);
    lv_obj_align(hint_label_, LV_ALIGN_CENTER, 0, 16);

    // NODES view: a fixed column header strip + a themed table. Hidden by default.
    nodes_view_ = lv_obj_create(content);
    lv_obj_remove_style_all(nodes_view_);
    lv_obj_set_size(nodes_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(nodes_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(nodes_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(nodes_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(nodes_view_, 0, 0);
    lv_obj_set_style_pad_row(nodes_view_, 0, 0);
    lv_obj_add_flag(nodes_view_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = lv_obj_create(nodes_view_);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kHeaderH);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    int32_t hx = 0;
    for (int c = 0; c < 4; ++c) {
        lv_obj_t* h = lv_label_create(header);
        lv_label_set_text(h, kColTitle[c]);
        lv_obj_set_style_text_font(h, fs, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0x888888), 0);
        lv_obj_align(h, LV_ALIGN_LEFT_MID, hx + 4, 0);
        hx += kColW[c];
    }

    nodes_table_ = lv_table_create(nodes_view_);
    lv_obj_set_width(nodes_table_, LV_PCT(100));
    lv_obj_set_flex_grow(nodes_table_, 1);
    lv_table_set_column_count(nodes_table_, 4);
    for (int c = 0; c < 4; ++c) lv_table_set_column_width(nodes_table_, c, kColW[c]);
    lv_obj_set_style_pad_all(nodes_table_, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_width(nodes_table_, 0, 0);
    lv_obj_set_style_text_font(nodes_table_, fs, LV_PART_ITEMS);
    lv_obj_remove_flag(nodes_table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(nodes_table_, nodes_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(nodes_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);
    tick(); // initial paint
}

void MeshtasticScreen::nodes_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<MeshtasticScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) return;
    const uint32_t row = base->id1;
    const bool is_sel = (static_cast<int>(row) == self->nodes_sel_row_);
    const lv_draw_task_type_t type = lv_draw_task_get_type(task);

    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel && base->part == LV_PART_ITEMS) {
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(self->vm_.is_dark_mode()).primary;
        fd->opa = LV_OPA_COVER;
        return;
    }
    if (type == LV_DRAW_TASK_TYPE_LABEL) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        if (is_sel) {
            ld->color = lv_color_black(); // contrast against the accent band
        } else if (row < self->nodes_row_colors_.size()) {
            ld->color = self->nodes_row_colors_[row];
        }
    }
}

void MeshtasticScreen::update_nodes(const std::vector<toolkit::Entity>& snap) {
    if (!nodes_table_) return;

    const auto pal = view::palette(vm_.is_dark_mode());
    const auto n = snap.size();
    lv_table_set_row_count(nodes_table_, static_cast<uint32_t>(n));
    nodes_row_colors_.assign(n, pal.text);

    const auto set_cell = [&](uint32_t r, uint16_t c, const char* v) {
        const char* cur = lv_table_get_cell_value(nodes_table_, r, c);
        if (!cur || std::strcmp(cur, v) != 0) {
            lv_table_set_cell_value(nodes_table_, r, c, v);
        }
    };

    const long now = static_cast<long>(std::time(nullptr));
    for (size_t i = 0; i < n; ++i) {
        const toolkit::Entity& e = snap[i];
        const bool self = !field_of(e, "self").empty();
        if (self) nodes_row_colors_[i] = pal.primary;

        std::string sh = field_of(e, "short");
        if (sh.empty()) sh = e.id;
        const std::string mark = self ? "\xE2\x97\x8f" : ""; // ● self marker
        const std::string call = mark + sh;

        const std::string snr = field_of(e, "snr");
        const std::string hops = field_of(e, "hops");
        const std::string lh = field_of(e, "last_heard");

        char age[16] = "-";
        if (!lh.empty()) {
            long a = now - std::atol(lh.c_str());
            if (a < 0) a = 0;
            if (a < 60) std::snprintf(age, sizeof(age), "%lds", a);
            else if (a < 3600) std::snprintf(age, sizeof(age), "%ldm", a / 60);
            else std::snprintf(age, sizeof(age), "%ldh", a / 3600);
        }

        set_cell(static_cast<uint32_t>(i), 0, call.c_str());
        set_cell(static_cast<uint32_t>(i), 1, snr.empty() ? "-" : snr.c_str());
        set_cell(static_cast<uint32_t>(i), 2, hops.empty() ? "-" : hops.c_str());
        set_cell(static_cast<uint32_t>(i), 3, age);
    }

    vm_.set_nodes_count(static_cast<int>(n));
    int cur = vm_.nodes_cursor();
    if (cur >= static_cast<int>(n)) cur = n > 0 ? static_cast<int>(n) - 1 : 0;
    nodes_sel_row_ = (n > 0) ? cur : -1;
    if (n > 0) lv_table_set_selected_cell(nodes_table_, static_cast<uint16_t>(cur), 0);
}

void MeshtasticScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<MeshtasticScreen*>(lv_timer_get_user_data(timer));
    if (self) self->tick();
}

void MeshtasticScreen::tick() {
    store_.sweep(kNodeTtlSeconds);
    const auto snap = store_.snapshot();
    const int page = vm_.page();

    char sub[48];
    std::snprintf(sub, sizeof(sub), "%s - %zu node%s", vm_.page_name(page),
                  snap.size(), snap.size() == 1 ? "" : "s");
    vm_.set_subtitle(sub);

    const bool nodes_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Nodes));
    lv_obj_set_flag(view_label_, LV_OBJ_FLAG_HIDDEN, nodes_page);
    lv_obj_set_flag(hint_label_, LV_OBJ_FLAG_HIDDEN, nodes_page);
    lv_obj_set_flag(nodes_view_, LV_OBJ_FLAG_HIDDEN, !nodes_page);

    if (nodes_page) {
        update_nodes(snap);
    } else {
        lv_label_set_text(view_label_, vm_.page_name(page));
    }
}

} // namespace meshtastic
