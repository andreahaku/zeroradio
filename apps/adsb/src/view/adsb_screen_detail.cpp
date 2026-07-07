/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_screen.h"
#include "adsb_screen_common.h"

#include <cstdio>
#include <string>

namespace adsb {

using namespace common;

void AdsbScreen::update_detail(const std::vector<Row>& rows) {
    if (!detail_label_) {
        return;
    }
    const int sel = row_of(rows, vm_.selected_hex());
    if (sel < 0) {
        // No selection: blank the data columns but still draw the full scope, so
        // the mini-radar looks and reads like the Radar screen.
        if (detail_values_)   lv_label_set_text(detail_values_, "-\n-\n-\n-\n-\n-");
        if (detail_values_b_) lv_label_set_text(detail_values_b_, "-\n-\n-\n-\n-");
        if (detail_msg_)      lv_label_set_text(detail_msg_, "(no aircraft selected)");
        render_scope(detail_buf_.data(), kDetailRadarSize, kDetailRadarSize, detail_canvas_,
                     detail_ring_labels_, rows, sel, vm_.detail_show_others());
        return;
    }
    const Row& r = rows[static_cast<size_t>(sel)];

    // ----- Left column: fields + decoded ADS-B messages / status -----
    const std::string alt_s = r.has_alt ? (std::to_string(r.alt) + "ft")
                              : r.on_ground ? std::string("ground")
                                            : std::string("-");
    const std::string gs_s  = r.has_gs ? (std::to_string(r.gs) + "kt") : std::string("-");
    const std::string trk_s = r.has_track ? (std::to_string(r.track) + "\xC2\xB0") : std::string("-");
    const bool km = vm_.units_km();
    const std::string rng_s =
        r.has_pos ? (std::to_string(static_cast<long>(to_unit(r.range_nm, km))) + dist_unit(km))
                  : std::string("-");
    const std::string brg_s = r.has_pos
                                  ? (std::to_string(static_cast<long>(r.bearing_deg)) + "\xC2\xB0")
                                  : std::string("-");
    // Decoded status messages from the squawk / flags.
    std::string msg;
    if (r.squawk == "7500")      msg = "HIJACK 7500";
    else if (r.squawk == "7600") msg = "RADIO FAIL 7600";
    else if (r.squawk == "7700") msg = "EMERGENCY 7700";
    else if (r.emergency)        msg = "EMERGENCY";
    if (r.on_ground) msg = msg.empty() ? "ON GROUND" : (msg + " / ON GROUND");
    if (msg.empty()) msg = "nominal";
    const std::string sig_s = r.has_rssi
                                  ? (std::to_string(static_cast<long>(r.rssi)) + " dBFS")
                                  : std::string("-");

    const std::string seen_s = r.has_seen ? (std::to_string(r.seen) + "s") : std::string("-");
    // Column A values (HEX/FLT/ALT/GS/TRK/SEEN), column B (SQK/CAT/RNG/BRG/SIG).
    char va[160];
    std::snprintf(va, sizeof(va), "%s\n%s\n%s\n%s\n%s\n%s",
                  r.hex.c_str(), r.flight.empty() ? "-" : r.flight.c_str(),
                  alt_s.c_str(), gs_s.c_str(), trk_s.c_str(), seen_s.c_str());
    char vb[160];
    std::snprintf(vb, sizeof(vb), "%s\n%s\n%s\n%s\n%s",
                  r.squawk.empty() ? "-" : r.squawk.c_str(),
                  r.category.empty() ? "-" : r.category.c_str(),
                  rng_s.c_str(), brg_s.c_str(), sig_s.c_str());
    if (detail_values_)   lv_label_set_text(detail_values_, va);
    if (detail_values_b_) lv_label_set_text(detail_values_b_, vb);
    if (detail_msg_)      lv_label_set_text(detail_msg_, msg.c_str());

    // ----- Right column: the radar scope. Same look as the Radar screen; the
    // Detail page's "show others" toggle picks all traffic vs the selected only.
    render_scope(detail_buf_.data(), kDetailRadarSize, kDetailRadarSize, detail_canvas_,
                 detail_ring_labels_, rows, sel, vm_.detail_show_others());
}

} // namespace adsb
