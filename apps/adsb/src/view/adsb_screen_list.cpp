/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_screen.h"

#include "geo.h"
#include "theme.h"
#include "adsb_screen_common.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace adsb {

using namespace common;

namespace {

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

std::vector<AdsbScreen::Row> AdsbScreen::build_all_rows() {
    auto snapshot = store_.snapshot();
    std::vector<Row> rows;
    rows.reserve(snapshot.size());

    for (const auto& e : snapshot) {
        Row r;
        r.hex = e.id;
        r.flight = field_str(e, "flight");
        r.has_pos = e.has_pos;
        r.pos = e.pos;
        // Drop a position that dump1090 reports as older than the TTL, so we stop
        // plotting an aircraft at a stale spot while it's still being heard. If
        // seen_pos is absent we keep the last-known position (prior behaviour).
        {
            bool has_seen_pos = false;
            const long seen_pos = field_long(e, "seen_pos", has_seen_pos);
            if (r.has_pos && has_seen_pos && static_cast<double>(seen_pos) > vm_.ttl_seconds()) {
                r.has_pos = false;
            }
        }
        r.alt = field_long(e, "alt", r.has_alt);
        r.on_ground = !field_str(e, "on_ground").empty();
        r.gs = field_long(e, "gs", r.has_gs);
        r.track = field_long(e, "track", r.has_track);
        r.squawk = field_str(e, "squawk");
        r.category = field_str(e, "category");
        r.emergency = field_str(e, "emergency") == "1";
        r.seen = field_long(e, "seen", r.has_seen);
        {
            const std::string rssi_s = field_str(e, "rssi");
            if (!rssi_s.empty()) {
                try { r.rssi = std::stod(rssi_s); r.has_rssi = true; } catch (...) {}
            }
        }
        if (r.has_pos) {
            r.range_nm = toolkit::geo::range_nm(config_.home, r.pos);
            r.bearing_deg = toolkit::geo::bearing_deg(config_.home, r.pos);
        }
        rows.push_back(std::move(r));
    }

    return rows;
}

void AdsbScreen::apply_filters_and_sort(std::vector<Row>& rows) {
    // Settings filters: hide on-ground traffic and/or show only emergencies.
    const bool hide_ground = !vm_.show_ground();
    const bool emerg_only = vm_.emergency_only();
    if (hide_ground || emerg_only) {
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&](const Row& r) {
                                      if (emerg_only && !r.emergency) return true;
                                      if (hide_ground && r.on_ground) return true;
                                      return false;
                                  }),
                   rows.end());
    }

    // Sort per the viewmodel. Aircraft missing the sort field go last.
    const auto call_of = [](const Row& r) -> const std::string& {
        return r.flight.empty() ? r.hex : r.flight;
    };
    switch (static_cast<AdsbViewModel::Sort>(vm_.sort_mode())) {
        case AdsbViewModel::Sort::Distance:
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                if (a.has_pos != b.has_pos) return a.has_pos; // positioned first
                return a.range_nm < b.range_nm;               // nearest first
            });
            break;
        case AdsbViewModel::Sort::Speed:
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                if (a.has_gs != b.has_gs) return a.has_gs;
                return a.gs > b.gs;                            // fastest first
            });
            break;
        case AdsbViewModel::Sort::Alt:
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                if (a.has_alt != b.has_alt) return a.has_alt;
                return a.alt > b.alt;                          // highest first
            });
            break;
        case AdsbViewModel::Sort::Track:
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
                if (a.has_track != b.has_track) return a.has_track;
                return a.track < b.track;                      // 0..360
            });
            break;
        case AdsbViewModel::Sort::Callsign:
        default:
            std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
                return call_of(a) < call_of(b);
            });
            break;
    }
}

int AdsbScreen::row_of(const std::vector<Row>& rows, const std::string& hex) const {
    if (hex.empty()) return -1;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].hex == hex) return static_cast<int>(i);
    }
    return -1;
}

void AdsbScreen::list_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<AdsbScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) {
        return;
    }
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) {
        return;
    }
    const uint32_t row = base->id1;          // table row index
    const bool is_sel = (static_cast<int>(row) == self->list_sel_row_);
    const lv_draw_task_type_t type = lv_draw_task_get_type(task);

    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel && base->part == LV_PART_ITEMS) {
        // Strong highlight: fill the selected row's cells with the accent colour.
        // Guard on LV_PART_ITEMS so the scrollbar (also id1==0) isn't recoloured.
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(false).primary;
        fd->opa = LV_OPA_COVER;
        return;
    }
    if (type == LV_DRAW_TASK_TYPE_LABEL) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        if (is_sel) {
            ld->color = lv_color_black();    // contrast against the accent (cursor) fill
        } else if (row < self->list_row_colors_.size()) {
            ld->color = self->list_row_colors_[row];
        }
    }
}

void AdsbScreen::update_list(const std::vector<Row>& rows) {
    if (!list_table_) {
        return;
    }

    // Data rows are 0-based (the column titles live in the fixed header strip).
    lv_table_set_row_count(list_table_, static_cast<uint32_t>(rows.size()));
    list_row_colors_.assign(rows.size(), lv_color_white());

    const std::string& sel_hex = vm_.selected_hex();
    const bool km = vm_.units_km();
    // Only write a cell when its text actually changed: lv_table_set_cell_value
    // frees+reallocs the cell string and dirties layout on every call, so an
    // unconditional rewrite each tick is a needless redraw cost on embedded.
    const auto set_cell = [&](uint32_t rr, uint16_t cc, const char* val) {
        const char* cur = lv_table_get_cell_value(list_table_, rr, cc);
        if (!cur || std::strcmp(cur, val) != 0) {
            lv_table_set_cell_value(list_table_, rr, cc, val);
        }
    };
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const uint32_t row = static_cast<uint32_t>(i);
        list_row_colors_[row] = category_color(r.category, r.emergency);
        // Sized for any 64-bit value: a bogus/huge field must not truncate to a
        // misleading number (snprintf is bounded either way).
        char alt_buf[24];
        char gs_buf[24];
        char trk_buf[16];
        char rng_buf[24];
        if (r.has_alt)        std::snprintf(alt_buf, sizeof(alt_buf), "%ld", r.alt);
        else if (r.on_ground) std::snprintf(alt_buf, sizeof(alt_buf), "grnd");
        else                  std::snprintf(alt_buf, sizeof(alt_buf), "-");
        if (r.has_gs)    std::snprintf(gs_buf, sizeof(gs_buf), "%ld", r.gs);
        else             std::snprintf(gs_buf, sizeof(gs_buf), "-");
        if (r.has_track) std::snprintf(trk_buf, sizeof(trk_buf), "%ld", r.track);
        else             std::snprintf(trk_buf, sizeof(trk_buf), "-");
        if (r.has_pos)   std::snprintf(rng_buf, sizeof(rng_buf), "%.0f", to_unit(r.range_nm, km));
        else             std::snprintf(rng_buf, sizeof(rng_buf), "-");

        // Mark the locked selection with a leading dot, emergency with "!".
        const char* mark = (r.hex == sel_hex && !sel_hex.empty()) ? "\xE2\x97\x8f" // ●
                           : (r.emergency ? "!" : "");
        const std::string call = std::string(mark) + (r.flight.empty() ? r.hex : r.flight);
        set_cell(row, 0, call.c_str());
        set_cell(row, 1, alt_buf);
        set_cell(row, 2, gs_buf);
        set_cell(row, 3, trk_buf);
        set_cell(row, 4, rng_buf);
    }

    // The cursor row gets the strong highlight (accent fill + black text).
    const int cur = row_of(rows, vm_.cursor_hex());
    list_sel_row_ = cur;
    if (cur >= 0) {
        lv_table_set_selected_cell(list_table_, static_cast<uint16_t>(cur), 0);
    }
}

} // namespace adsb
