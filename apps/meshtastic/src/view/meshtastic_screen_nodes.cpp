/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "geo.h"
#include "theme.h"
#include "meshtastic_screen_common.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace meshtastic {

using namespace common;

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
    // Report the cursor node-num to the VM so detail + DM know the target.
    if (cur >= 0 && static_cast<size_t>(cur) < snap.size()) {
        const std::string& id = snap[static_cast<size_t>(cur)].id;
        if (id.size() > 1 && id[0] == '!') {
            const uint32_t num =
                static_cast<uint32_t>(std::strtoul(id.c_str() + 1, nullptr, 16));
            vm_.set_selected_node(num);
        }
    }
}

namespace {

const char* hw_name(int hw) {
    switch (hw) {
        case 0:   return "UNSET";
        case 3:   return "TLORA-V2";
        case 4:   return "T-BEAM";
        case 9:   return "RAK4631";
        case 10:  return "HELTEC-V2";
        case 37:  return "PORTDUINO";
        case 43:  return "HELTEC-V3";
        case 50:  return "T-DECK";
        case 71:  return "T1000-E";
        case 81:  return "XIAO-S3";
        default:  return "HW?";
    }
}

const char* role_name(int r) {
    switch (r) {
        case 0:  return "CLIENT";
        case 1:  return "MUTE";
        case 2:  return "ROUTER";
        case 3:  return "RTR-CLI";
        case 4:  return "REPEAT";
        case 5:  return "TRACKER";
        case 6:  return "SENSOR";
        default: return "?";
    }
}

} // namespace (detail helpers)

void MeshtasticScreen::update_node_detail(const std::vector<toolkit::Entity>& snap) {
    if (!node_detail_) return;
    const uint32_t num = vm_.selected_node();
    // Find the entity — id is "!<hex8>".
    const toolkit::Entity* ent = nullptr;
    for (const auto& e : snap) {
        if (e.id.size() > 1 && e.id[0] == '!') {
            if (std::strtoul(e.id.c_str() + 1, nullptr, 16) == num) {
                ent = &e;
                break;
            }
        }
    }
    if (!ent) {
        lv_label_set_text(node_detail_, "#888888 (no node selected)#");
        return;
    }
    const auto& e = *ent;
    const bool is_self = !field_of(e, "self").empty();
    const uint32_t accent = is_self ? 0x63e2b7u : 0x70c0e8u;
    const std::string sh   = field_of(e, "short");
    const std::string lng  = field_of(e, "long");
    const std::string snr  = field_of(e, "snr");
    const std::string hops = field_of(e, "hops");
    const std::string lh   = field_of(e, "last_heard");
    const std::string hw   = field_of(e, "hw");
    const std::string role = field_of(e, "role");
    const std::string batt = field_of(e, "batt");
    const std::string volt = field_of(e, "volt");
    char buf[512];
    // Header line: short · long · id
    std::snprintf(buf, sizeof(buf), "#%06x %s  %s  %s#\n",
                  accent,
                  sh.empty() ? "-" : sh.c_str(),
                  lng.empty() ? "" : lng.c_str(),
                  e.id.c_str());
    std::string text = buf;
    // HW / ROLE row
    const char* hw_s   = hw.empty()   ? "-" : hw_name(std::atoi(hw.c_str()));
    const char* role_s = role.empty() ? "-" : role_name(std::atoi(role.c_str()));
    std::snprintf(buf, sizeof(buf), "#888888 HW#  %-10s  #888888 ROLE#  %s\n", hw_s, role_s);
    text += buf;
    // SNR / HOPS row
    std::snprintf(buf, sizeof(buf), "#888888 SNR#  %-7s  #888888 HOPS#  %s\n",
                  snr.empty() ? "-" : (snr + " dB").c_str(),
                  hops.empty() ? "-" : hops.c_str());
    text += buf;
    // BATT / VOLT row
    if (!batt.empty() || !volt.empty()) {
        const int bv = batt.empty() ? -1 : std::atoi(batt.c_str());
        const char* batt_s = batt.empty() ? "-" : (bv > 100 ? "USB" : (batt + "%").c_str());
        std::snprintf(buf, sizeof(buf), "#888888 BATT#  %-7s  #888888 VOLT#  %sV\n",
                      batt_s, volt.empty() ? "-" : volt.c_str());
        text += buf;
    }
    // POS / DIST / BRG — compute from the self node.
    if (e.has_pos) {
        char pos_s[48];
        std::snprintf(pos_s, sizeof(pos_s), "%.4f, %.4f", e.pos.lat, e.pos.lon);
        std::snprintf(buf, sizeof(buf), "#888888 POS#  %s\n", pos_s);
        text += buf;
        for (const auto& s : snap) {
            if (field_of(s, "self").empty() || !s.has_pos) continue;
            const double dist_nm  = toolkit::geo::range_nm(s.pos, e.pos);
            const double dist_km  = dist_nm * 1.852;
            const double brg      = toolkit::geo::bearing_deg(s.pos, e.pos);
            char dist_s[16];
            if (dist_km < 1.0) std::snprintf(dist_s, sizeof(dist_s), "%.0fm", dist_km * 1000.0);
            else                std::snprintf(dist_s, sizeof(dist_s), "%.1fkm", dist_km);
            std::snprintf(buf, sizeof(buf), "#888888 DIST#  %-8s  #888888 BRG#  %.0f\xC2\xB0\n",
                          dist_s, brg);
            text += buf;
            break;
        }
    }
    // Last-heard
    if (!lh.empty()) {
        const long now = static_cast<long>(std::time(nullptr));
        long age = now - std::atol(lh.c_str());
        if (age < 0) age = 0;
        char age_s[24];
        if (age < 60)        std::snprintf(age_s, sizeof(age_s), "%lds", age);
        else if (age < 3600) std::snprintf(age_s, sizeof(age_s), "%ldm", age / 60);
        else                 std::snprintf(age_s, sizeof(age_s), "%ldh", age / 3600);
        std::snprintf(buf, sizeof(buf), "#888888 HEARD#  %s ago\n", age_s);
        text += buf;
    }
    lv_label_set_text(node_detail_, text.c_str());
}

} // namespace meshtastic
