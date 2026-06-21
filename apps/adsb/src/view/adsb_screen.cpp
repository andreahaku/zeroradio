/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "geo.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_set>

namespace adsb {
namespace {

// Refresh period: the decode rate is low (tens of msgs/s), so a few Hz is plenty
// (much lighter than SDRTerminal's 30 fps waterfall).
constexpr uint32_t kTickPeriodMs = 300;

// Header height inside the content area.
constexpr int32_t kHeaderHeight = 18;

// PPI canvas geometry: a square that fits the body (screen 170 - nav 30 - header
// 18 = ~122px tall), kept 4-byte-stride aligned (120*2 = 240 bytes). A larger
// square would be clipped vertically by the body.
constexpr int32_t kPpiSize = 120;

// Detail mini-radar: a square on the right of the split Detail view.
constexpr int32_t kDetailRadarSize = 118; // 118*2 = 236 bytes (4-byte aligned)

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

// RGB encoding the aircraft category (emergency overrides to red).
uint32_t category_rgb(const std::string& category, bool emergency) {
    if (emergency) return 0xff5050;
    if (category == "light")      return 0x6fd66f; // green
    if (category == "small")      return 0x66ccff; // cyan
    if (category == "large")      return 0x4d9fff; // blue
    if (category == "heavy")      return 0xffb347; // orange
    if (category == "rotorcraft") return 0xc792ea; // purple
    return 0xc8c8c8;                               // other/unknown: grey
}

lv_color_t category_color(const std::string& category, bool emergency) {
    return lv_color_hex(category_rgb(category, emergency));
}

// Plot a filled disc of `r` px around (cx, cy) in the RGB565 buffer.
void plot_disc(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || x >= w || y < 0 || y >= h) continue;
            buf[y * w + x] = color;
        }
    }
}

// Plot a thin ring (midpoint circle) of radius `r` around (cx, cy).
void plot_ring(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t color) {
    int x = r;
    int y = 0;
    int err = 1 - r;
    const auto put = [&](int px, int py) {
        if (px >= 0 && px < w && py >= 0 && py < h) buf[py * w + px] = color;
    };
    while (x >= y) {
        put(cx + x, cy + y); put(cx - x, cy + y);
        put(cx + x, cy - y); put(cx - x, cy - y);
        put(cx + y, cy + x); put(cx - y, cy + x);
        put(cx + y, cy - x); put(cx - y, cy - x);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

// Fill a triangle (small markers) by bounding-box + half-plane test.
void plot_triangle(uint16_t* buf, int w, int h, int x0, int y0, int x1, int y1,
                   int x2, int y2, uint16_t color) {
    const int minx = std::max(0, std::min({x0, x1, x2}));
    const int maxx = std::min(w - 1, std::max({x0, x1, x2}));
    const int miny = std::max(0, std::min({y0, y1, y2}));
    const int maxy = std::min(h - 1, std::max({y0, y1, y2}));
    const auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            const int w0 = edge(x1, y1, x2, y2, x, y);
            const int w1 = edge(x2, y2, x0, y0, x, y);
            const int w2 = edge(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                buf[y * w + x] = color;
            }
        }
    }
}

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

// Plot a straight line (Bresenham) from (x0,y0) to (x1,y1).
void plot_line(uint16_t* buf, int w, int h, int x0, int y0, int x1, int y1, uint16_t color) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) buf[y0 * w + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Distance helpers for the units setting (NM default, km optional).
inline double to_unit(double nm, bool km) { return km ? nm * 1.852 : nm; }
inline const char* dist_unit(bool km) { return km ? "km" : "NM"; }

} // namespace

AdsbScreen::AdsbScreen(AdsbViewModel& vm,
                       app::AssetManager& assets,
                       toolkit::EntityStore& store,
                       const toolkit::Config& config,
                       std::function<bool()> conn_state)
    : BaseScreen(vm, vm, assets),
      vm_(vm),
      store_(store),
      config_(config),
      conn_state_(std::move(conn_state)),
      ppi_buf_(static_cast<size_t>(kPpiSize) * kPpiSize, 0u),
      detail_buf_(static_cast<size_t>(kDetailRadarSize) * kDetailRadarSize, 0u) {
    init();
}

AdsbScreen::~AdsbScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void AdsbScreen::build_content(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 0, 0);

    font_small_ = assets().load_font("inter-regular.ttf", 12);
    font_mono_  = assets().load_font("inter-semibold.ttf", 12);
    font_bold_  = assets().load_font("inter-bold.ttf", 12);

    // --- Header row: title | "N trk" | conn dot ---
    header_ = lv_obj_create(content);
    lv_obj_remove_style_all(header_);
    lv_obj_set_size(header_, LV_PCT(100), kHeaderHeight);
    lv_obj_clear_flag(header_, LV_OBJ_FLAG_SCROLLABLE);

    // Left: one screen-relevant info (count / range / …), set per screen in tick.
    header_count_ = lv_label_create(header_);
    lv_label_set_text(header_count_, "");
    lv_obj_set_style_text_font(header_count_, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(header_count_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(header_count_, LV_ALIGN_LEFT_MID, 4, 0);

    // Centre: the selected aircraft (callsign + active-sort value).
    header_title_ = lv_label_create(header_);
    lv_label_set_text(header_title_, "ADSB");
    lv_obj_set_style_text_font(header_title_, font_mono_ ? font_mono_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(header_title_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(header_title_, LV_ALIGN_CENTER, 0, 0);

    // Signal-quality bar (strongest aircraft RSSI), like the SDR S-meter.
    sig_bar_ = lv_bar_create(header_);
    lv_obj_set_size(sig_bar_, 30, 6);
    lv_bar_set_range(sig_bar_, 0, 100);
    lv_obj_set_style_bg_color(sig_bar_, view::palette(false).primary, LV_PART_INDICATOR);
    lv_obj_remove_flag(sig_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(sig_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(sig_bar_, LV_ALIGN_RIGHT_MID, -18, 0);

    conn_dot_ = lv_obj_create(header_);
    lv_obj_remove_style_all(conn_dot_);
    lv_obj_set_size(conn_dot_, 8, 8);
    lv_obj_set_style_radius(conn_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(conn_dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(conn_dot_, lv_color_hex(0x888888), 0);
    lv_obj_clear_flag(conn_dot_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(conn_dot_, LV_ALIGN_RIGHT_MID, -6, 0);

    // --- Body: holds the three interchangeable views (one shown at a time). ---
    body_ = lv_obj_create(content);
    lv_obj_remove_style_all(body_);
    lv_obj_set_width(body_, LV_PCT(100));
    lv_obj_set_flex_grow(body_, 1);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(body_, 0, 0);

    // List view: a fixed column-header strip on top + a scrolling 5-column table
    // (callsign / altitude / speed / track / distance). The header stays put while
    // the table scrolls, so columns are always labelled. Data rows are 0-based.
    static constexpr int32_t kColW[5] = {92, 58, 48, 46, 60};
    static const char* kColTitle[5] = {"CALL", "ALT", "SPD", "TRK", "DST"};
    constexpr int32_t kListHeaderH = 15;

    list_view_ = lv_obj_create(body_);
    lv_obj_remove_style_all(list_view_);
    lv_obj_set_size(list_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(list_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(list_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(list_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_view_, 0, 0);
    lv_obj_set_style_pad_row(list_view_, 0, 0);

    // Fixed header strip: column titles at the same x as the table columns.
    lv_obj_t* list_header = lv_obj_create(list_view_);
    lv_obj_remove_style_all(list_header);
    lv_obj_set_size(list_header, LV_PCT(100), kListHeaderH);
    lv_obj_clear_flag(list_header, LV_OBJ_FLAG_SCROLLABLE);
    int32_t hx = 0;
    for (int c = 0; c < 5; ++c) {
        lv_obj_t* h = lv_label_create(list_header);
        lv_label_set_text(h, kColTitle[c]);
        lv_obj_set_style_text_font(h, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
        reactive::bind_theme(h, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
        lv_obj_align(h, LV_ALIGN_LEFT_MID, hx + 3, 0);
        hx += kColW[c];
    }

    list_table_ = lv_table_create(list_view_);
    lv_obj_set_width(list_table_, LV_PCT(100));
    lv_obj_set_flex_grow(list_table_, 1);
    lv_table_set_column_count(list_table_, 5);
    for (int c = 0; c < 5; ++c) lv_table_set_column_width(list_table_, c, kColW[c]);
    lv_obj_set_style_pad_all(list_table_, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_width(list_table_, 0, 0);
    if (font_small_) {
        lv_obj_set_style_text_font(list_table_, font_small_, LV_PART_ITEMS);
    }
    lv_obj_remove_flag(list_table_, LV_OBJ_FLAG_CLICKABLE);
    // Per-row colouring (category / emergency) via the draw-task event.
    lv_obj_add_event_cb(list_table_, list_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(list_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    // PPI view (RGB565 canvas, square, centred).
    ppi_canvas_ = lv_canvas_create(body_);
    lv_canvas_set_buffer(ppi_canvas_, ppi_buf_.data(), kPpiSize, kPpiSize, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(ppi_canvas_, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_size(ppi_canvas_, kPpiSize, kPpiSize);
    lv_obj_align(ppi_canvas_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(ppi_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ppi_canvas_, LV_OBJ_FLAG_CLICKABLE);

    // Range-ring scale labels: one tiny NM label per concentric ring, dim so it
    // reads as chrome. Positioned along the north axis in update_ppi.
    ppi_ring_labels_.reserve(3);
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* lbl = lv_label_create(body_);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x7aa07a), 0);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        ppi_ring_labels_.push_back(lbl);
    }

    // Radar side lists: all aircraft callsigns flanking the scope (left/right),
    // colour-coded by category, with the cursor/selection marked. Recolour labels
    // so each line can take its own colour. Display-only here (navigate on List).
    const auto make_side = [&](lv_align_t align, int32_t xoff) {
        lv_obj_t* l = lv_label_create(body_);
        lv_label_set_recolor(l, true);
        lv_label_set_text(l, "");
        lv_obj_set_width(l, 98);
        lv_obj_set_style_text_font(l, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(l, align, xoff, 0);
        return l;
    };
    radar_left_  = make_side(LV_ALIGN_TOP_LEFT, 1);
    radar_right_ = make_side(LV_ALIGN_TOP_RIGHT, -1);
    lv_obj_set_style_text_align(radar_right_, LV_TEXT_ALIGN_RIGHT, 0);

    // Detail view (all fields of the selected aircraft as label rows).
    detail_box_ = lv_obj_create(body_);
    lv_obj_remove_style_all(detail_box_);
    lv_obj_set_size(detail_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(detail_box_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(detail_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(detail_box_, 4, 0);
    // Two field/value columns on the left (names bold to stand out), with a
    // decoded-message line underneath; the mini-radar sits on the right.
    const lv_font_t* fr = font_small_ ? font_small_ : &lv_font_montserrat_12;
    const lv_font_t* fb = font_bold_ ? font_bold_ : fr;
    const auto mk = [&](int32_t x, int32_t y, const lv_font_t* f, int32_t wdt) {
        lv_obj_t* l = lv_label_create(detail_box_);
        lv_label_set_text(l, "");
        if (wdt > 0) lv_obj_set_width(l, wdt);
        lv_obj_set_style_text_font(l, f, 0);
        reactive::bind_theme(l, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y);
        return l;
    };
    detail_label_    = mk(0,   0, fb, 0);   // column A names (bold)
    detail_values_   = mk(40,  0, fr, 52);  // column A values (clear of "SEEN")
    detail_names_b_  = mk(98,  0, fb, 0);   // column B names (bold)
    detail_values_b_ = mk(130, 0, fr, 56);  // column B values
    detail_msg_      = mk(0, 104, fb, 190); // decoded message line (wraps), clear of SEEN
    lv_label_set_long_mode(detail_msg_, LV_LABEL_LONG_WRAP);
    // Static field names.
    lv_label_set_text(detail_label_,   "HEX\nFLT\nALT\nGS\nTRK\nSEEN");
    lv_label_set_text(detail_names_b_, "SQK\nCAT\nRNG\nBRG\nSIG");

    // Right: mini-radar showing only the selected aircraft + its trail.
    detail_canvas_ = lv_canvas_create(detail_box_);
    lv_canvas_set_buffer(detail_canvas_, detail_buf_.data(), kDetailRadarSize, kDetailRadarSize,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(detail_canvas_, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_size(detail_canvas_, kDetailRadarSize, kDetailRadarSize);
    lv_obj_align(detail_canvas_, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_remove_flag(detail_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(detail_canvas_, LV_OBJ_FLAG_CLICKABLE);

    // Range-ring scale labels for the mini-radar (same as the PPI's). Children of
    // detail_box_ so they hide with it; positioned over the canvas in render_scope.
    detail_ring_labels_.reserve(3);
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* lbl = lv_label_create(detail_box_);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x7aa07a), 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        detail_ring_labels_.push_back(lbl);
    }

    // Settings view: a 2-column table (name | value) with the focused row
    // highlighted, matching the List screen's look.
    settings_box_ = lv_obj_create(body_);
    lv_obj_remove_style_all(settings_box_);
    lv_obj_set_size(settings_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(settings_box_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(settings_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(settings_box_, 0, 0);

    settings_table_ = lv_table_create(settings_box_);
    lv_obj_set_size(settings_table_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(settings_table_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_table_set_column_count(settings_table_, 2);
    lv_table_set_column_width(settings_table_, 0, 150); // name
    lv_table_set_column_width(settings_table_, 1, 158); // value
    lv_obj_set_style_pad_ver(settings_table_, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(settings_table_, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_width(settings_table_, 0, 0);
    if (font_mono_) lv_obj_set_style_text_font(settings_table_, font_mono_, LV_PART_ITEMS);
    lv_obj_remove_flag(settings_table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings_table_, settings_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(settings_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    // Start on the viewmodel's current screen.
    show_view(vm_.screen());

    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);
}

void AdsbScreen::show_view(int screen) {
    last_view_ = screen;
    const bool list     = (screen == static_cast<int>(AdsbViewModel::Screen::List));
    const bool ppi      = (screen == static_cast<int>(AdsbViewModel::Screen::Radar));
    const bool detail   = (screen == static_cast<int>(AdsbViewModel::Screen::Detail));
    const bool settings = (screen == static_cast<int>(AdsbViewModel::Screen::Settings));

    if (list_view_)     lv_obj_set_flag(list_view_,     LV_OBJ_FLAG_HIDDEN, !list);
    if (ppi_canvas_)    lv_obj_set_flag(ppi_canvas_,    LV_OBJ_FLAG_HIDDEN, !ppi);
    if (radar_left_)    lv_obj_set_flag(radar_left_,    LV_OBJ_FLAG_HIDDEN, !ppi);
    if (radar_right_)   lv_obj_set_flag(radar_right_,   LV_OBJ_FLAG_HIDDEN, !ppi);
    if (detail_box_)    lv_obj_set_flag(detail_box_,    LV_OBJ_FLAG_HIDDEN, !detail);
    if (settings_box_)  lv_obj_set_flag(settings_box_,  LV_OBJ_FLAG_HIDDEN, !settings);

    // The PPI callsign labels only belong to the Radar view; hide them otherwise
    // (update_ppi re-shows the ones it uses on each Radar tick).
    if (!ppi) {
        for (lv_obj_t* lbl : ppi_ring_labels_) {
            if (lbl) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

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

void AdsbScreen::update_header(int signal_quality) {
    // The left info label and centre title are set in tick() (screen-dependent);
    // here we only drive the signal bar and the connection dot.
    const auto& pal = view::palette(vm_.is_dark_mode());
    if (sig_bar_) {
        lv_bar_set_value(sig_bar_, signal_quality, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(sig_bar_, pal.primary, LV_PART_INDICATOR);
    }
    if (conn_dot_) {
        const bool ok = conn_state_ ? conn_state_() : false;
        lv_obj_set_style_bg_color(conn_dot_,
                                  ok ? pal.primary : lv_color_hex(0x888888), 0);
    }
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

void AdsbScreen::render_scope(uint16_t* buf, int size, lv_obj_t* canvas,
                             std::vector<lv_obj_t*>& ring_labels,
                             const std::vector<Row>& rows, int sel, bool show_others) {
    if (!canvas) return;
    const int w = size, h = size;
    std::fill(buf, buf + static_cast<size_t>(w) * h, lv_color_to_u16(lv_color_black()));

    const int cx = w / 2, cy = h / 2;
    const int radius_px = (std::min(w, h) / 2) - 4;

    const uint16_t ring_col = lv_color_to_u16(lv_color_hex(0x224422));
    const uint16_t north_col = lv_color_to_u16(lv_color_hex(0x66aa66));
    const uint16_t home_col = lv_color_to_u16(lv_color_white());
    const uint16_t ac_col = lv_color_to_u16(view::palette(false).primary);
    const uint16_t emg_col = lv_color_to_u16(lv_color_hex(0xff4040));
    const uint16_t sel_ac_col = lv_color_to_u16(lv_color_hex(0xff9933)); // selected: orange
    const uint16_t trail_col = lv_color_to_u16(lv_color_hex(0x55aa55));  // others' trail
    const uint16_t trail_sel_col = sel_ac_col;                          // selected trail

    // Concentric range rings (1/3, 2/3, full).
    const int ring_px[3] = {radius_px / 3, (radius_px * 2) / 3, radius_px};
    for (int rp : ring_px) plot_ring(buf, w, h, cx, cy, rp, ring_col);

    // North tick (short line up from centre) + home dot.
    for (int y = cy - radius_px; y < cy - radius_px + 8; ++y)
        if (y >= 0 && y < h) buf[y * w + cx] = north_col;
    plot_disc(buf, w, h, cx, cy, 2, home_col);

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

    const bool trails_on = vm_.show_trails(); // length is the Trails setting; on/off is the key
    const double max_nm = static_cast<double>(vm_.range_nm());
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
                if (!toolkit::geo::project(config_.home, p, max_nm, radius_px, tx, ty)) {
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
        if (!toolkit::geo::project(config_.home, r.pos, max_nm, radius_px, dx, dy)) continue;
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
    render_scope(ppi_buf_.data(), kPpiSize, ppi_canvas_, ppi_ring_labels_, rows, sel,
                 /*show_others=*/true);

    // Side callsign lists (all aircraft), colour-coded, with the selection (●) and
    // cursor (›) marked. First half on the left column, the rest on the right.
    if (radar_left_ && radar_right_) {
        const std::string& cur = vm_.cursor_hex();
        const std::string& selh = vm_.selected_hex();
        constexpr size_t kPerSide = 11;
        const size_t left_n = (rows.size() + 1) / 2;
        std::string left, right;
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            const std::string call = r.flight.empty() ? r.hex : r.flight;
            const char* mk = (!selh.empty() && r.hex == selh) ? "\xE2\x97\x8f"  // ●
                             : (!cur.empty() && r.hex == cur) ? "\xE2\x80\xBA" // ›
                                                              : " ";
            // Wide enough that a long flight string can't truncate away the
            // closing "#" — a dropped recolor terminator would bleed the colour
            // into the rest of the label.
            char line[128];
            std::snprintf(line, sizeof(line), "#%06X %s%s#\n",
                          category_rgb(r.category, r.emergency), mk, call.c_str());
            std::string& col = (i < left_n) ? left : right;
            // Cap each column so it fits the scope height.
            const size_t shown = (i < left_n) ? i : (i - left_n);
            if (shown < kPerSide) col += line;
        }
        lv_label_set_text(radar_left_, left.c_str());
        lv_label_set_text(radar_right_, right.c_str());
    }
}

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
        render_scope(detail_buf_.data(), kDetailRadarSize, detail_canvas_,
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
    render_scope(detail_buf_.data(), kDetailRadarSize, detail_canvas_,
                 detail_ring_labels_, rows, sel, vm_.detail_show_others());
}

void AdsbScreen::settings_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<AdsbScreen*>(lv_event_get_user_data(event));
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

void AdsbScreen::update_settings() {
    if (!settings_table_) {
        return;
    }
    const int n = vm_.settings_count();
    lv_table_set_row_count(settings_table_, static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) {
        lv_table_set_cell_value(settings_table_, static_cast<uint32_t>(i), 0,
                                vm_.setting_name(i).c_str());
        lv_table_set_cell_value(settings_table_, static_cast<uint32_t>(i), 1,
                                vm_.setting_value(i).c_str());
    }
    settings_sel_row_ = vm_.settings_cursor();
    if (settings_sel_row_ >= 0) {
        lv_table_set_selected_cell(settings_table_, static_cast<uint16_t>(settings_sel_row_), 0);
    }
}

void AdsbScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<AdsbScreen*>(lv_timer_get_user_data(timer));
    if (self) {
        self->tick();
    }
}

void AdsbScreen::tick() {
    // Drop stale entries (TTL from settings), then snapshot.
    store_.sweep(vm_.ttl_seconds());
    auto rows = build_all_rows();
    // Record trails from the *unfiltered* rows (every tick, regardless of the
    // visible screen) so toggling a filter doesn't wipe hidden aircraft history.
    record_trails(rows);
    apply_filters_and_sort(rows);

    // Report the current sorted aircraft order so prev/next move by identity and
    // a vanished selection snaps to the nearest aircraft.
    std::vector<std::string> order;
    order.reserve(rows.size());
    double max_nm = 0.0;
    bool any_rssi = false;
    double best_rssi = -120.0;
    for (const auto& r : rows) {
        order.push_back(r.hex);
        if (r.has_pos && r.range_nm > max_nm) max_nm = r.range_nm;
        if (r.has_rssi && r.rssi > best_rssi) { best_rssi = r.rssi; any_rssi = true; }
    }
    vm_.set_visible_order(std::move(order));
    vm_.set_observed_max_nm(max_nm); // feeds the auto range (before header/PPI use it)

    // Signal quality from the strongest RSSI (dBFS): map ~[-30,-3] dB -> [0,100].
    int signal_quality = 0;
    if (any_rssi) {
        const double q = (best_rssi + 30.0) / 27.0 * 100.0;
        signal_quality = q < 0.0 ? 0 : (q > 100.0 ? 100 : static_cast<int>(q));
    }

    const int screen = vm_.screen();
    if (screen != last_view_) {
        show_view(screen);
    }

    const int sel_row = row_of(rows, vm_.selected_hex());
    const bool km = vm_.units_km();

    // Centre title: selected callsign + the value of the active sort field.
    if (header_title_) {
        if (sel_row >= 0) {
            const Row& r = rows[static_cast<size_t>(sel_row)];
            const std::string call = r.flight.empty() ? r.hex : r.flight;
            char sv[16] = "";
            switch (static_cast<AdsbViewModel::Sort>(vm_.sort_mode())) {
                case AdsbViewModel::Sort::Distance:
                    if (r.has_pos)
                        std::snprintf(sv, sizeof(sv), " %.0f%s", to_unit(r.range_nm, km),
                                      dist_unit(km));
                    break;
                case AdsbViewModel::Sort::Speed:
                    if (r.has_gs) std::snprintf(sv, sizeof(sv), " %ldkt", r.gs);
                    break;
                case AdsbViewModel::Sort::Alt:
                    if (r.has_alt) std::snprintf(sv, sizeof(sv), " %ldft", r.alt);
                    else if (r.on_ground) std::snprintf(sv, sizeof(sv), " grnd");
                    break;
                case AdsbViewModel::Sort::Track:
                    if (r.has_track) std::snprintf(sv, sizeof(sv), " %ld\xC2\xB0", r.track);
                    break;
                case AdsbViewModel::Sort::Callsign:
                default:
                    break;
            }
            char t[48];
            std::snprintf(t, sizeof(t), "%s%s", call.c_str(), sv);
            lv_label_set_text(header_title_, t);
        } else {
            lv_label_set_text(header_title_, "ADSB");
        }
    }

    // Left: one screen-relevant info.
    if (header_count_) {
        char info[32];
        switch (static_cast<AdsbViewModel::Screen>(vm_.screen())) {
            case AdsbViewModel::Screen::Radar:
                std::snprintf(info, sizeof(info), "%s%.0f%s", vm_.auto_range() ? "auto " : "",
                              to_unit(vm_.range_nm(), km), dist_unit(km));
                break;
            case AdsbViewModel::Screen::Detail:
                if (sel_row >= 0 && rows[static_cast<size_t>(sel_row)].has_pos)
                    std::snprintf(info, sizeof(info), "%.0f%s",
                                  to_unit(rows[static_cast<size_t>(sel_row)].range_nm, km),
                                  dist_unit(km));
                else
                    std::snprintf(info, sizeof(info), "detail");
                break;
            case AdsbViewModel::Screen::Settings:
                std::snprintf(info, sizeof(info), "settings");
                break;
            case AdsbViewModel::Screen::List:
            default:
                std::snprintf(info, sizeof(info), "%d trk", static_cast<int>(rows.size()));
                break;
        }
        lv_label_set_text(header_count_, info);
    }

    update_header(signal_quality);

    switch (static_cast<AdsbViewModel::Screen>(screen)) {
        case AdsbViewModel::Screen::List:   update_list(rows); break;
        case AdsbViewModel::Screen::Radar:  update_ppi(rows); break;
        case AdsbViewModel::Screen::Detail: update_detail(rows); break;
        case AdsbViewModel::Screen::Settings: update_settings(); break;
    }
}

} // namespace adsb
