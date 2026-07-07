/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_screen.h"

#include "ais_decoder.h"
#include "ais_screen_common.h"

#include <cstdio>
#include <string>

namespace ais {

using namespace common;

void AisScreen::update_detail(const std::vector<Row>& rows) {
    if (!detail_label_) return;
    const int sel = row_of(rows, vm_.selected_id());
    if (sel < 0) {
        if (detail_values_)   lv_label_set_text(detail_values_, "-\n-\n-\n-\n-");
        if (detail_values_b_) lv_label_set_text(detail_values_b_, "-\n-\n-");
        if (detail_msg_)      lv_label_set_text(detail_msg_, "(no vessel selected)");
        render_scope(detail_buf_.data(), kDetailRadarSize, kDetailRadarSize, detail_canvas_,
                     detail_ring_labels_, rows, sel, vm_.detail_show_others());
        return;
    }
    const Row& r = rows[static_cast<size_t>(sel)];

    const bool km = vm_.units_km();
    char sog_s[24], cog_s[24], hdg_s[24];
    if (r.has_sog) std::snprintf(sog_s, sizeof(sog_s), "%.1fkt", r.sog);
    else           std::snprintf(sog_s, sizeof(sog_s), "-");
    if (r.has_cog) std::snprintf(cog_s, sizeof(cog_s), "%.0f\xC2\xB0", r.cog);
    else           std::snprintf(cog_s, sizeof(cog_s), "-");
    if (r.has_hdg) std::snprintf(hdg_s, sizeof(hdg_s), "%ld\xC2\xB0", r.hdg);
    else           std::snprintf(hdg_s, sizeof(hdg_s), "-");
    const std::string rng_s =
        r.has_pos ? (std::to_string(static_cast<long>(to_unit(r.range_nm, km))) + dist_unit(km))
                  : std::string("-");
    const std::string brg_s =
        r.has_pos ? (std::to_string(static_cast<long>(r.bearing_deg)) + "\xC2\xB0")
                  : std::string("-");
    const std::string seen_s = r.has_seen ? (std::to_string(r.seen) + "s") : std::string("-");
    const std::string stat_s = r.nav_status >= 0 ? std::to_string(r.nav_status) : std::string("-");

    // The wide bottom line carries the type-5 identity (callsign / ship type /
    // destination) plus the nav-status text — the long strings that don't fit the
    // two-column grid. The vessel name is already the header title, so it is not
    // repeated here. Built from whatever is known; " · "-separated.
    std::string msg;
    const auto add = [&msg](const std::string& part) {
        if (part.empty()) return;
        if (!msg.empty()) msg += " \xC2\xB7 "; // · separator
        msg += part;
    };
    if (!r.callsign.empty()) add("(" + r.callsign + ")");
    if (r.ship_type != 0) {
        const char* tl = ais::ship_type_label(r.ship_type);
        add((tl && tl[0]) ? std::string(tl) : ("type " + std::to_string(r.ship_type)));
    }
    if (!r.destination.empty()) add("\xE2\x86\x92" + r.destination); // →DEST
    const char* nav_txt = nav_status_text(r.nav_status);
    if (nav_txt && nav_txt[0]) add(nav_txt);
    if (msg.empty()) msg = "nominal";

    char va[192];
    std::snprintf(va, sizeof(va), "%s\n%s\n%s\n%s\n%s",
                  r.id.c_str(), sog_s, cog_s, hdg_s, seen_s.c_str());
    char vb[96];
    std::snprintf(vb, sizeof(vb), "%s\n%s\n%s", stat_s.c_str(), rng_s.c_str(), brg_s.c_str());
    if (detail_values_)   lv_label_set_text(detail_values_, va);
    if (detail_values_b_) lv_label_set_text(detail_values_b_, vb);
    if (detail_msg_)      lv_label_set_text(detail_msg_, msg.c_str());

    render_scope(detail_buf_.data(), kDetailRadarSize, kDetailRadarSize, detail_canvas_,
                 detail_ring_labels_, rows, sel, vm_.detail_show_others());
}

} // namespace ais
