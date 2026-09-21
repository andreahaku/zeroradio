/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "geo.h"
#include "raster.h"
#include "map_renderer.h"
#include "meshtastic_screen_common.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace meshtastic {

using namespace common;

namespace {

// Self node colour (theme accent green); each peer gets a distinct hue so its
// radar dot and its side-list name share one colour — that's how you read which
// dot is which without cramming labels onto the tiny scope.
constexpr uint32_t kSelfColor = 0x63e2b7;
constexpr uint32_t kNodeColors[] = {
    0x4d9fff, // blue
    0xffa53d, // orange
    0xe24dff, // magenta
    0xffe24d, // yellow
    0xc77dff, // violet
    0xff5d6e, // red
    0x4de2e2, // cyan
    0xff8fbf, // pink
};
constexpr int kNodeColorCount = static_cast<int>(sizeof(kNodeColors) / sizeof(kNodeColors[0]));

// RGB565 raster primitives (disc/ring) are shared with the ADS-B/AIS scopes —
// see toolkit view::raster.
using view::plot_disc;
using view::plot_ring;

} // namespace

void MeshtasticScreen::update_map(const std::vector<toolkit::Entity>& snap) {
    if (!map_canvas_ || !map_canvas_merc_) return;
    const bool mercator = vm_.map_mercator();
    // Swap which fixed-size canvas is visible; the radar is square (106), the
    // Mercator map is twice as wide (212) to use more of the screen.
    lv_obj_t* canvas = mercator ? map_canvas_merc_ : map_canvas_;
    std::vector<uint16_t>& buf_vec = mercator ? map_buf_merc_ : map_buf_;
    lv_obj_add_flag(mercator ? map_canvas_ : map_canvas_merc_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);

    const int w = mercator ? kMapMercW : kMapSize;
    const int h = mercator ? kMapMercH : kMapSize;
    const int cx = w / 2, cy = h / 2;
    const int radius_px = (std::min(w, h) / 2) - 4;
    uint16_t* buf = buf_vec.data();

    std::fill(buf_vec.begin(), buf_vec.end(), lv_color_to_u16(lv_color_black()));

    const uint16_t ring_col  = lv_color_to_u16(lv_color_hex(0x224422));
    const uint16_t north_col = lv_color_to_u16(lv_color_hex(0x66aa66));
    const uint16_t nofix_col = lv_color_to_u16(lv_color_hex(0x555555));
    const uint16_t outline_col = lv_color_to_u16(lv_color_white());

    // Rings + north tick (radar mode). The Mercator view draws the coastline /
    // border base layer instead, once `home` is known (see below). ring_px stays
    // defined either way so the scale labels can align to it.
    const int ring_px[3] = {radius_px / 3, (radius_px * 2) / 3, radius_px};
    if (!mercator) {
        for (int rp : ring_px) plot_ring(buf, w, h, cx, cy, rp, ring_col);
        for (int y = cy - radius_px; y < cy - radius_px + 6; ++y)
            if (y >= 0 && y < h) buf[y * w + cx] = north_col;
    }

    // Positioned nodes (snapshot order = stable) + the self node (if present).
    std::vector<const toolkit::Entity*> pos;
    const toolkit::Entity* self = nullptr;
    for (const auto& e : snap) {
        if (!field_of(e, "self").empty()) self = &e;
        if (e.has_pos) pos.push_back(&e);
    }
    vm_.set_map_count(static_cast<int>(pos.size()));
    const int cursor = vm_.map_cursor();

    // Per-node colour: self = accent green; peers cycle a distinct palette so a
    // dot and its side-list name share one colour.
    std::vector<uint32_t> colors(pos.size(), kSelfColor);
    int peer_ord = 0;
    for (size_t i = 0; i < pos.size(); ++i) {
        if (pos[i] == self) colors[i] = kSelfColor;
        else colors[i] = kNodeColors[(peer_ord++) % kNodeColorCount];
    }

    // Home point: the self node if it has a fix, else the centroid of all
    // positioned nodes.
    bool have_home = false;
    bool home_is_self = false;
    toolkit::geo::LatLon home{};
    if (self && self->has_pos) {
        home = self->pos;
        have_home = true;
        home_is_self = true;
    } else if (!pos.empty()) {
        double slat = 0.0, slon = 0.0;
        for (auto* e : pos) { slat += e->pos.lat; slon += e->pos.lon; }
        home.lat = slat / static_cast<double>(pos.size());
        home.lon = slon / static_cast<double>(pos.size());
        have_home = true;
    }

    if (!have_home) {
        plot_disc(buf, w, h, cx, cy, 2, nofix_col);
        for (auto* lbl : map_ring_labels_)
            if (lbl) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        for (auto* l : map_left_rows_)  lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        for (auto* l : map_right_rows_) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        if (map_status_) lv_label_set_text(map_status_, "no positioned nodes");
        lv_obj_invalidate(canvas);
        return;
    }

    // Auto-fit the outer ring to the farthest node, fed back so the zoom keys can
    // snap relative to it.
    double fit_km = 0.0;
    for (auto* e : pos) {
        const double km = toolkit::geo::range_nm(home, e->pos) * 1.852;
        if (km > fit_km) fit_km = km;
    }
    vm_.set_map_fit_km(fit_km);
    const double range_km = vm_.map_range_km();
    const double range_nm = range_km / 1.852;

    // Mercator base map: coastline + national borders centred on home, scaled so
    // the outer-ring distance spans the canvas vertically. Drawn under the dots.
    if (mercator && base_map_.valid()) {
        toolkit::map::MapViewport vp;
        vp.width = w;
        vp.height = h;
        vp.cx = cx;
        vp.cy = cy;
        vp.radius_px = radius_px;
        vp.home = home;
        vp.range_nm = range_nm;
        vp.projection = toolkit::map::Projection::Mercator;
        map_cache_.draw(buf, vp, base_map_);
    }

    // Range-ring scale labels (km), one per ring on the north axis — radar only.
    if (mercator) {
        for (auto* lbl : map_ring_labels_)
            if (lbl) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        for (size_t i = 0; i < map_ring_labels_.size(); ++i) {
            lv_obj_t* lbl = map_ring_labels_[i];
            if (!lbl) continue;
            const double v = range_km * (static_cast<double>(i) + 1) / 3.0;
            char t[16];
            if (v < 1.0) std::snprintf(t, sizeof(t), "%.0fm", v * 1000.0);
            else std::snprintf(t, sizeof(t), "%.0fkm", v);
            lv_label_set_text(lbl, t);
            lv_obj_remove_flag(lbl, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align_to(lbl, map_canvas_, LV_ALIGN_CENTER, 6, -ring_px[i] + 6);
        }
    }

    // Home dot at the centre (self green, or grey when it's a centroid).
    const bool home_sel = (cursor >= 0 && static_cast<size_t>(cursor) < pos.size() &&
                           pos[cursor] == self);
    if (home_sel) plot_disc(buf, w, h, cx, cy, 6, outline_col);
    plot_disc(buf, w, h, cx, cy, home_is_self ? 4 : 3,
              lv_color_to_u16(lv_color_hex(home_is_self ? kSelfColor : 0x999999)));

    // One dot per positioned peer; the selected node gets a white outline.
    for (size_t i = 0; i < pos.size(); ++i) {
        if (pos[i] == self) continue; // already the centre dot
        int dx = 0, dy = 0;
        const bool ok = mercator
            ? toolkit::geo::project_mercator(home, pos[i]->pos, range_nm, radius_px, dx, dy)
            : toolkit::geo::project(home, pos[i]->pos, range_nm, radius_px, dx, dy);
        if (!ok) continue;
        // Mercator doesn't cull, so drop dots that fall outside the canvas.
        if (mercator && (cx + dx < 0 || cx + dx >= w || cy + dy < 0 || cy + dy >= h)) continue;
        const bool is_sel = (static_cast<int>(i) == cursor);
        if (is_sel) plot_disc(buf, w, h, cx + dx, cy + dy, 6, outline_col);
        plot_disc(buf, w, h, cx + dx, cy + dy, is_sel ? 5 : 4,
                  lv_color_to_u16(lv_color_hex(colors[i])));
    }

    // Side short-name columns (pool of row labels). Each row matches its dot's
    // colour; the selected node renders inverted (colour fill + black text), and
    // the self node renders bold.
    const lv_font_t* fs  = font_small_      ? font_small_      : &lv_font_montserrat_12;
    const lv_font_t* fsb = font_small_bold_ ? font_small_bold_ : fs;
    const size_t left_n = (pos.size() + 1) / 2;
    size_t li = 0, ri = 0;
    for (size_t i = 0; i < pos.size(); ++i) {
        const bool on_left = (i < left_n);
        auto& pool = on_left ? map_left_rows_ : map_right_rows_;
        size_t& slot = on_left ? li : ri;
        if (slot >= pool.size()) continue;
        lv_obj_t* l = pool[slot++];

        std::string sh = field_of(*pos[i], "short");
        if (sh.empty()) sh = pos[i]->id;
        lv_label_set_text(l, sh.c_str());

        const bool is_self = (pos[i] == self);
        const bool is_sel  = (static_cast<int>(i) == cursor);
        lv_obj_set_style_text_font(l, is_self ? fsb : fs, 0);
        if (is_sel) {
            lv_obj_set_style_bg_color(l, lv_color_hex(colors[i]), 0);
            lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(l, lv_color_black(), 0);
        } else {
            lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(colors[i]), 0);
        }
        lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t k = li; k < map_left_rows_.size(); ++k)
        lv_obj_add_flag(map_left_rows_[k], LV_OBJ_FLAG_HIDDEN);
    for (size_t k = ri; k < map_right_rows_.size(); ++k)
        lv_obj_add_flag(map_right_rows_[k], LV_OBJ_FLAG_HIDDEN);

    if (map_status_) {
        char s[40];
        const char* view = mercator ? "map" : "ppi";
        const char* mode = vm_.map_auto_range() ? "auto" : "rng";
        if (range_km < 1.0)
            std::snprintf(s, sizeof(s), "%s %s %.0fm", view, mode, range_km * 1000.0);
        else
            std::snprintf(s, sizeof(s), "%s %s %.0fkm", view, mode, range_km);
        lv_label_set_text(map_status_, s);
    }
    lv_obj_invalidate(canvas);
}

} // namespace meshtastic
