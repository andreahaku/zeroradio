/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_screen.h"

#include "ais_decoder.h"
#include "geo.h"
#include "theme.h"
#include "ais_screen_common.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace ais {

using namespace common;

namespace {

double field_double(const toolkit::Entity& e, const char* key, bool& has) {
    auto it = e.fields.find(key);
    has = (it != e.fields.end() && !it->second.empty());
    if (!has) return 0.0;
    try {
        return std::stod(it->second);
    } catch (...) {
        has = false;
        return 0.0;
    }
}

long field_long(const toolkit::Entity& e, const char* key, bool& has) {
    auto it = e.fields.find(key);
    has = (it != e.fields.end() && !it->second.empty());
    if (!has) return 0;
    try {
        return std::stol(it->second);
    } catch (...) {
        has = false;
        return 0;
    }
}

std::string field_str(const toolkit::Entity& e, const char* key) {
    auto it = e.fields.find(key);
    return it != e.fields.end() ? it->second : std::string{};
}

} // namespace

std::vector<AisScreen::Row> AisScreen::build_all_rows() {
    auto snapshot = store_.snapshot();
    std::vector<Row> rows;
    rows.reserve(snapshot.size());

    for (const auto& e : snapshot) {
        Row r;
        r.id = e.id;
        r.name = field_str(e, "name");
        r.callsign = field_str(e, "callsign");
        r.destination = field_str(e, "destination");
        {
            bool has_type = false;
            r.ship_type = static_cast<int>(field_long(e, "shiptype", has_type));
        }
        r.has_pos = e.has_pos;
        r.pos = e.pos;
        r.sog = field_double(e, "sog", r.has_sog);
        r.cog = field_double(e, "cog", r.has_cog);
        r.hdg = field_long(e, "hdg", r.has_hdg);
        {
            bool has_status = false;
            const long st = field_long(e, "status", has_status);
            r.nav_status = has_status ? static_cast<int>(st) : -1;
        }
        r.seen = field_long(e, "seen", r.has_seen);
        if (r.has_pos) {
            r.range_nm = toolkit::geo::range_nm(config_.home, r.pos);
            r.bearing_deg = toolkit::geo::bearing_deg(config_.home, r.pos);
        }
        rows.push_back(std::move(r));
    }

    return rows;
}

void AisScreen::apply_sort(std::vector<Row>& rows) {
    switch (static_cast<AisViewModel::Sort>(vm_.sort_mode())) {
        case AisViewModel::Sort::Distance:
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                if (a.has_pos != b.has_pos) return a.has_pos;
                return a.range_nm < b.range_nm;
            });
            break;
        case AisViewModel::Sort::Sog:
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                if (a.has_sog != b.has_sog) return a.has_sog;
                return a.sog > b.sog;
            });
            break;
        case AisViewModel::Sort::Cog:
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                if (a.has_cog != b.has_cog) return a.has_cog;
                return a.cog < b.cog;
            });
            break;
        case AisViewModel::Sort::Mmsi:
        default:
            std::sort(rows.begin(), rows.end(),
                      [](const Row& a, const Row& b) { return a.id < b.id; });
            break;
    }
}

int AisScreen::row_of(const std::vector<Row>& rows, const std::string& id) const {
    if (id.empty()) return -1;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

void AisScreen::list_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<AisScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) return;
    const uint32_t row = base->id1;
    const bool is_sel = (static_cast<int>(row) == self->list_sel_row_);
    const lv_draw_task_type_t type = lv_draw_task_get_type(task);

    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel && base->part == LV_PART_ITEMS) {
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(false).primary;
        fd->opa = LV_OPA_COVER;
        return;
    }
    if (type == LV_DRAW_TASK_TYPE_LABEL) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        if (is_sel) {
            ld->color = lv_color_black();
        } else if (row < self->list_row_colors_.size()) {
            ld->color = self->list_row_colors_[row];
        }
    }
}

void AisScreen::update_list(const std::vector<Row>& rows) {
    if (!list_table_) return;

    lv_table_set_row_count(list_table_, static_cast<uint32_t>(rows.size()));
    list_row_colors_.assign(rows.size(), lv_color_white());

    const std::string& sel_id = vm_.selected_id();
    const bool km = vm_.units_km();
    const auto set_cell = [&](uint32_t rr, uint16_t cc, const char* val) {
        const char* cur = lv_table_get_cell_value(list_table_, rr, cc);
        if (!cur || std::strcmp(cur, val) != 0) {
            lv_table_set_cell_value(list_table_, rr, cc, val);
        }
    };
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const uint32_t row = static_cast<uint32_t>(i);
        list_row_colors_[row] = nav_color(r.nav_status);

        char sog_buf[24];
        char cog_buf[24];
        char hdg_buf[16];
        char rng_buf[24];
        if (r.has_sog) std::snprintf(sog_buf, sizeof(sog_buf), "%.1f", r.sog);
        else           std::snprintf(sog_buf, sizeof(sog_buf), "-");
        if (r.has_cog) std::snprintf(cog_buf, sizeof(cog_buf), "%.0f", r.cog);
        else           std::snprintf(cog_buf, sizeof(cog_buf), "-");
        if (r.has_hdg) std::snprintf(hdg_buf, sizeof(hdg_buf), "%ld", r.hdg);
        else           std::snprintf(hdg_buf, sizeof(hdg_buf), "-");
        if (r.has_pos) std::snprintf(rng_buf, sizeof(rng_buf), "%.0f", to_unit(r.range_nm, km));
        else           std::snprintf(rng_buf, sizeof(rng_buf), "-");

        const char* mark = (r.id == sel_id && !sel_id.empty()) ? "\xE2\x97\x8f" : "";
        const std::string label = std::string(mark) + (r.name.empty() ? r.id : r.name);
        set_cell(row, 0, label.c_str());
        set_cell(row, 1, sog_buf);
        set_cell(row, 2, cog_buf);
        set_cell(row, 3, hdg_buf);
        set_cell(row, 4, rng_buf);
    }

    const int cur = row_of(rows, vm_.cursor_id());
    list_sel_row_ = cur;
    if (cur >= 0) {
        lv_table_set_selected_cell(list_table_, static_cast<uint16_t>(cur), 0);
    }
}

} // namespace ais
