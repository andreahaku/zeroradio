/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_screen.h"

#include "geo.h"
#include "raster.h"
#include "theme.h"
#include "adsb_screen_common.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

namespace adsb {

using namespace common;

namespace {

// RGB565 raster primitives (disc/ring/triangle/line) are shared with the AIS
// scope — see toolkit view::raster. Only the aircraft marker below is app-specific.
using view::plot_disc;
using view::plot_line;
using view::plot_ring;
using view::plot_triangle;

// Draw an aircraft marker at (px,py): an arrowhead pointing along `track_deg`
// (north-up), or a small diamond when the track is unknown. `scale` enlarges the
// selected aircraft.
void plot_aircraft(uint16_t* buf, int w, int h, int px, int py, bool has_track,
                   long track_deg, uint16_t color, float scale) {
    if (!has_track) {
        const int r = static_cast<int>(2 * scale + 0.5f);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (std::abs(dx) + std::abs(dy) <= r) {
                    const int x = px + dx, y = py + dy;
                    if (x >= 0 && x < w && y >= 0 && y < h) buf[y * w + x] = color;
                }
        return;
    }
    const double t = static_cast<double>(track_deg) * 3.14159265358979323846 / 180.0;
    const double ux = std::sin(t), uy = -std::cos(t);   // forward (north-up)
    const double vx = -uy, vy = ux;                      // perpendicular
    const double tip = 6.0 * scale, back = 3.0 * scale, half = 3.5 * scale;
    const int tx = px + static_cast<int>(std::lround(ux * tip));
    const int ty = py + static_cast<int>(std::lround(uy * tip));
    const double bx = px - ux * back, by = py - uy * back;
    const int lx = static_cast<int>(std::lround(bx + vx * half));
    const int ly = static_cast<int>(std::lround(by + vy * half));
    const int rx = static_cast<int>(std::lround(bx - vx * half));
    const int ry = static_cast<int>(std::lround(by - vy * half));
    plot_triangle(buf, w, h, tx, ty, lx, ly, rx, ry, color);
}

} // namespace

void AdsbScreen::record_trails(const std::vector<Row>& rows) {
    // Append each aircraft's current position to its history and drop vanished
    // ones. Runs every tick (independent of the visible screen) so trails stay
    // current on both the radar and the Detail mini-radar.
    // trail_len 0 = "All" (don't expire); keep a safety cap so memory is bounded.
    const size_t cap = vm_.trail_len() > 0 ? static_cast<size_t>(vm_.trail_len()) : 600;
    std::unordered_set<std::string> live;
    live.reserve(rows.size());
    for (const auto& r : rows) {
        if (!r.has_pos) continue;
        live.insert(r.hex);
        auto& hist = trails_[r.hex];
        if (hist.empty() || hist.back().lat != r.pos.lat || hist.back().lon != r.pos.lon) {
            hist.push_back(r.pos);
        }
        while (hist.size() > cap) hist.pop_front();
    }
    // Prune vanished aircraft in place rather than rebuilding the whole map each
    // tick (avoids per-tick node/string allocation churn on the embedded heap).
    for (auto it = trails_.begin(); it != trails_.end();) {
        it = (live.count(it->first) == 0) ? trails_.erase(it) : std::next(it);
    }
}

void AdsbScreen::render_scope(uint16_t* buf, int width, int height, lv_obj_t* canvas,
                             std::vector<lv_obj_t*>& ring_labels,
                             const std::vector<Row>& rows, int sel, bool show_others,
                             bool mercator) {
    if (!canvas) return;
    const int w = width, h = height;
    std::fill(buf, buf + static_cast<size_t>(w) * h, lv_color_to_u16(lv_color_black()));

    const int cx = w / 2, cy = h / 2;
    const int radius_px = (std::min(w, h) / 2) - 4;
    const double max_nm = static_cast<double>(vm_.range_nm());

    const uint16_t ring_col = lv_color_to_u16(lv_color_hex(0x224422));
    const uint16_t north_col = lv_color_to_u16(lv_color_hex(0x66aa66));
    const uint16_t home_col = lv_color_to_u16(lv_color_white());
    const uint16_t ac_col = lv_color_to_u16(view::palette(false).primary);
    const uint16_t emg_col = lv_color_to_u16(lv_color_hex(0xff4040));
    const uint16_t sel_ac_col = lv_color_to_u16(lv_color_hex(0xff9933)); // selected: orange
    const uint16_t trail_col = lv_color_to_u16(lv_color_hex(0x55aa55));  // others' trail
    const uint16_t trail_sel_col = sel_ac_col;                          // selected trail

    if (mercator) {
        // Map mode: coastline + national borders centred on home, instead of rings.
        toolkit::map::MapViewport vp;
        vp.width = w;
        vp.height = h;
        vp.cx = cx;
        vp.cy = cy;
        vp.radius_px = radius_px;
        vp.home = config_.home;
        vp.range_nm = max_nm;
        vp.projection = toolkit::map::Projection::Mercator;
        auto& cache = (canvas == detail_canvas_) ? detail_map_cache_ : scope_map_cache_;
        cache.draw(buf, vp, base_map_);
        for (lv_obj_t* lbl : ring_labels)
            if (lbl) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Concentric range rings (1/3, 2/3, full).
        const int ring_px[3] = {radius_px / 3, (radius_px * 2) / 3, radius_px};
        for (int rp : ring_px) plot_ring(buf, w, h, cx, cy, rp, ring_col);

        // North tick (short line up from centre).
        for (int y = cy - radius_px; y < cy - radius_px + 8; ++y)
            if (y >= 0 && y < h) buf[y * w + cx] = north_col;

        // Range-ring scale labels (NM), one per ring, over the canvas's north axis.
        const int ring_nm = vm_.range_nm();
        const bool ring_km = vm_.units_km();
        for (size_t i = 0; i < ring_labels.size(); ++i) {
            lv_obj_t* lbl = ring_labels[i];
            if (!lbl) continue;
            lv_label_set_text_fmt(lbl, "%.0f",
                                  to_unit(ring_nm * (static_cast<double>(i) + 1) / 3.0, ring_km));
            lv_obj_remove_flag(lbl, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align_to(lbl, canvas, LV_ALIGN_CENTER, 4, -ring_px[i] + 6);
        }
    }
    // Home dot at the centre (both modes).
    plot_disc(buf, w, h, cx, cy, 2, home_col);

    // Project a point with the active projection. Mercator (map) doesn't cull, so
    // drop points that fall outside the canvas; azimuthal (radar) culls by range.
    auto project_pt = [&](const toolkit::geo::LatLon& p, int& dx, int& dy) -> bool {
        if (mercator) {
            if (!toolkit::geo::project_mercator(config_.home, p, max_nm, radius_px, dx, dy))
                return false;
            return cx + dx >= 0 && cx + dx < w && cy + dy >= 0 && cy + dy < h;
        }
        return toolkit::geo::project(config_.home, p, max_nm, radius_px, dx, dy);
    };

    const bool trails_on = vm_.show_trails(); // length is the Trails setting; on/off is the key
    const std::string& sel_hex = vm_.selected_hex();

    // Trails are recorded once per tick in record_trails(); here we only draw the
    // recent track of each aircraft when enabled.
    if (trails_on) {
        for (const auto& r : rows) {
            if (!r.has_pos) continue;
            // When "show others" is off, draw only the selected aircraft's trail.
            if (!show_others && (sel_hex.empty() || r.hex != sel_hex)) continue;
            auto it = trails_.find(r.hex);
            if (it == trails_.end() || it->second.size() < 2) continue;
            const uint16_t tc = (!sel_hex.empty() && r.hex == sel_hex) ? trail_sel_col : trail_col;
            int pdx = 0, pdy = 0;
            bool have_prev = false;
            for (const auto& p : it->second) {
                int tx = 0, ty = 0;
                if (!project_pt(p, tx, ty)) {
                    have_prev = false;
                    continue;
                }
                if (have_prev) plot_line(buf, w, h, cx + pdx, cy + pdy, cx + tx, cy + ty, tc);
                pdx = tx; pdy = ty; have_prev = true;
            }
        }
    }

    // One arrowhead per positioned aircraft (pointing along its track): emergency
    // red, the selected contact orange and larger, everyone else primary green.
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        if (!r.has_pos) continue;
        // When "show others" is off, draw only the selected aircraft.
        if (!show_others && static_cast<int>(i) != sel) continue;
        int dx = 0, dy = 0;
        if (!project_pt(r.pos, dx, dy)) continue;
        const bool is_sel = (static_cast<int>(i) == sel);
        const uint16_t col = r.emergency ? emg_col : (is_sel ? sel_ac_col : ac_col);
        plot_aircraft(buf, w, h, cx + dx, cy + dy, r.has_track, r.track, col,
                      is_sel ? 1.6f : 1.0f);
    }

    // Compact trails indicator: a small dot in the top-left corner.
    plot_disc(buf, w, h, 6, 6, 3,
              lv_color_to_u16(trails_on ? lv_color_hex(0x66cc66) : lv_color_hex(0x3a3a3a)));

    lv_obj_invalidate(canvas);
}

void AdsbScreen::update_ppi(const std::vector<Row>& rows) {
    const int sel = row_of(rows, vm_.selected_hex());
    const bool mercator = vm_.map_mercator();

    // Swap which fixed-size canvas is visible (square radar vs wide Mercator map).
    lv_obj_t* canvas = mercator ? ppi_canvas_merc_ : ppi_canvas_;
    uint16_t* buf = mercator ? ppi_buf_merc_.data() : ppi_buf_.data();
    // Mercator map is a wide 320x120 canvas; the PPI radar is square. Pass the
    // real width/height so render_scope's fill/centre never assume a square (a
    // square h here overran the 320x120 mercator buffer -> heap corruption).
    const int cw = mercator ? kPpiMercW : kPpiSize;
    const int ch = mercator ? kPpiMercH : kPpiSize;
    if (ppi_canvas_)      lv_obj_set_flag(ppi_canvas_,      LV_OBJ_FLAG_HIDDEN, mercator);
    if (ppi_canvas_merc_) lv_obj_set_flag(ppi_canvas_merc_, LV_OBJ_FLAG_HIDDEN, !mercator);

    render_scope(buf, cw, ch, canvas, ppi_ring_labels_, rows, sel,
                 /*show_others=*/true, mercator);

    // Side callsign columns stay visible in both modes — in map mode the map runs
    // full-width and the labels sit over it with a transparent background (left
    // column left-aligned, right column right-aligned), so you get both at once.

    // Side callsign lists (all aircraft), colour-coded. Each row renders into the
    // pool: the selected aircraft inverted (category colour fill + black text),
    // the cursor marked with a leading ›, everyone else coloured text on a
    // transparent background. First half on the left column, the rest on the right.
    const std::string& cur = vm_.cursor_hex();
    const std::string& selh = vm_.selected_hex();
    const size_t left_n = (rows.size() + 1) / 2;
    size_t li = 0, ri = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const bool on_left = (i < left_n);
        auto& pool = on_left ? radar_left_rows_ : radar_right_rows_;
        size_t& slot = on_left ? li : ri;
        if (slot >= pool.size()) continue;
        lv_obj_t* l = pool[slot++];

        const Row& r = rows[i];
        const std::string call = r.flight.empty() ? r.hex : r.flight;
        const bool is_sel = (!selh.empty() && r.hex == selh);
        const bool is_cur = (!is_sel && !cur.empty() && r.hex == cur);
        lv_label_set_text(l, (is_cur ? "\xE2\x80\xBA" + call : call).c_str()); // › cursor

        if (is_sel) {
            lv_obj_set_style_bg_color(l, lv_color_hex(category_rgb(r.category, r.emergency)), 0);
            lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(l, lv_color_black(), 0);
        } else {
            lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(category_rgb(r.category, r.emergency)), 0);
        }
        lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t k = li; k < radar_left_rows_.size(); ++k)
        lv_obj_add_flag(radar_left_rows_[k], LV_OBJ_FLAG_HIDDEN);
    for (size_t k = ri; k < radar_right_rows_.size(); ++k)
        lv_obj_add_flag(radar_right_rows_[k], LV_OBJ_FLAG_HIDDEN);
}

} // namespace adsb
