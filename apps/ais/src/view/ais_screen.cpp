/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_screen.h"

#include "ais_decoder.h"
#include "asset_manager.h"
#include "bindings.h"
#include "geo.h"
#include "raster.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace ais {
namespace {

constexpr uint32_t kTickPeriodMs = 300;
constexpr int32_t kHeaderHeight = 18;
constexpr int32_t kPpiSize = 120;
constexpr int32_t kPpiMercW = 320;
constexpr int32_t kPpiMercH = 120;
constexpr int32_t kDetailRadarSize = 118;

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

// Short text for an AIS navigation status (ITU-R M.1371).
const char* nav_status_text(int s) {
    switch (s) {
        case 0:  return "Under way (engine)";
        case 1:  return "At anchor";
        case 2:  return "Not under command";
        case 3:  return "Restricted manoeuvr.";
        case 4:  return "Constrained draught";
        case 5:  return "Moored";
        case 6:  return "Aground";
        case 7:  return "Fishing";
        case 8:  return "Under way (sailing)";
        case 11: return "Towing astern";
        case 12: return "Pushing ahead";
        case 14: return "AIS-SART";
        default: return "";
    }
}

// RGB encoding the vessel by navigation status (mirrors ADS-B's category colour).
uint32_t nav_rgb(int s) {
    switch (s) {
        case 0: case 8:  return 0x6fd66f; // under way: green
        case 1: case 5:  return 0x88a0c0; // anchored / moored: blue-grey
        case 7:          return 0x66ccff; // fishing: cyan
        case 2: case 3:
        case 4: case 6:  return 0xffb347; // impaired / aground: orange
        default:         return 0xc8c8c8; // unknown/other: grey
    }
}

lv_color_t nav_color(int s) { return lv_color_hex(nav_rgb(s)); }

// RGB565 raster primitives (disc/ring/triangle/line) are shared with the ADS-B
// scope — see toolkit view::raster. Only the vessel marker below is app-specific.
using view::plot_disc;
using view::plot_line;
using view::plot_ring;
using view::plot_triangle;

// Draw a vessel marker at (px,py): an arrowhead pointing along its course (COG,
// north-up), or a small diamond when no course/heading is known. `scale`
// enlarges the selected vessel.
void plot_vessel(uint16_t* buf, int w, int h, int px, int py, bool has_dir,
                 double dir_deg, uint16_t color, float scale) {
    if (!has_dir) {
        const int r = static_cast<int>(2 * scale + 0.5f);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (std::abs(dx) + std::abs(dy) <= r) {
                    const int x = px + dx, y = py + dy;
                    if (x >= 0 && x < w && y >= 0 && y < h) buf[y * w + x] = color;
                }
        return;
    }
    const double t = dir_deg * 3.14159265358979323846 / 180.0;
    const double ux = std::sin(t), uy = -std::cos(t);
    const double vx = -uy, vy = ux;
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

inline double to_unit(double nm, bool km) { return km ? nm * 1.852 : nm; }
inline const char* dist_unit(bool km) { return km ? "km" : "NM"; }

} // namespace

AisScreen::AisScreen(AisViewModel& vm,
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
      ppi_buf_merc_(static_cast<size_t>(kPpiMercW) * kPpiMercH, 0u),
      detail_buf_(static_cast<size_t>(kDetailRadarSize) * kDetailRadarSize, 0u) {
    init();
}

AisScreen::~AisScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void AisScreen::build_content(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 0, 0);

    font_small_ = assets().load_font("inter-regular.ttf", 12);
    font_mono_  = assets().load_font("inter-semibold.ttf", 12);
    font_bold_  = assets().load_font("inter-bold.ttf", 12);
    font_tiny_  = assets().load_font("inter-regular.ttf", 10);

    base_map_ = toolkit::map::VectorMap::load(
        assets().resolve("mapdata/world.rmap").string());

    // --- Header row: count | title | conn dot ---
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
    lv_label_set_text(header_title_, "AIS");
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

    // --- Body ---
    body_ = lv_obj_create(content);
    lv_obj_remove_style_all(body_);
    lv_obj_set_width(body_, LV_PCT(100));
    lv_obj_set_flex_grow(body_, 1);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(body_, 0, 0);

    // List view: a fixed column-header strip + a scrolling 5-column table
    // (MMSI / SOG / COG / HDG / distance).
    static constexpr int32_t kColW[5] = {92, 50, 48, 48, 56};
    static const char* kColTitle[5] = {"MMSI", "SOG", "COG", "HDG", "DST"};
    constexpr int32_t kListHeaderH = 15;

    list_view_ = lv_obj_create(body_);
    lv_obj_remove_style_all(list_view_);
    lv_obj_set_size(list_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(list_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(list_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(list_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_view_, 0, 0);
    lv_obj_set_style_pad_row(list_view_, 0, 0);

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

    ppi_canvas_merc_ = lv_canvas_create(body_);
    lv_canvas_set_buffer(ppi_canvas_merc_, ppi_buf_merc_.data(), kPpiMercW, kPpiMercH,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(ppi_canvas_merc_, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_size(ppi_canvas_merc_, kPpiMercW, kPpiMercH);
    lv_obj_align(ppi_canvas_merc_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(ppi_canvas_merc_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ppi_canvas_merc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ppi_canvas_merc_, LV_OBJ_FLAG_HIDDEN);

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

    // Radar side lists: all vessel ids flanking the scope, colour-coded by nav
    // status. The selected one renders inverted; the cursor gets a leading ›.
    constexpr int kSideRows = 11;
    constexpr int kSideRowH = 11;
    const lv_font_t* side_font = font_tiny_ ? font_tiny_ : &lv_font_montserrat_12;
    const auto make_side = [&](lv_align_t align, int32_t xoff) {
        lv_obj_t* c = lv_obj_create(body_);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 100, kSideRows * kSideRowH);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(c, align, xoff, 0);
        return c;
    };
    radar_left_  = make_side(LV_ALIGN_TOP_LEFT, 1);
    radar_right_ = make_side(LV_ALIGN_TOP_RIGHT, -1);

    const auto make_rows = [&](lv_obj_t* parent, std::vector<lv_obj_t*>& pool, bool right) {
        for (int i = 0; i < kSideRows; ++i) {
            lv_obj_t* l = lv_label_create(parent);
            lv_obj_set_style_text_font(l, side_font, 0);
            lv_obj_set_style_pad_hor(l, 2, 0);
            lv_obj_set_style_radius(l, 2, 0);
            lv_obj_align(l, right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, i * kSideRowH);
            if (right) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
            pool.push_back(l);
        }
    };
    make_rows(radar_left_, radar_left_rows_, false);
    make_rows(radar_right_, radar_right_rows_, true);

    // Detail view.
    detail_box_ = lv_obj_create(body_);
    lv_obj_remove_style_all(detail_box_);
    lv_obj_set_size(detail_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(detail_box_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(detail_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(detail_box_, 4, 0);
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
    // A 9-digit MMSI needs a wider value column than ADS-B's 6-char hex, so the
    // left value column is widened and column B shifted right (still clear of the
    // mini-radar that sits on the right edge).
    detail_label_    = mk(0,   0, fb, 0);
    detail_values_   = mk(38,  0, fr, 74);
    detail_names_b_  = mk(116, 0, fb, 0);
    detail_values_b_ = mk(150, 0, fr, 46);
    // Smaller font + wrap for the identity line: it can run long (callsign · ship
    // type · destination · nav status) and must fit the width left of the radar.
    detail_msg_      = mk(0, 102, font_tiny_ ? font_tiny_ : fr, 192);
    lv_label_set_long_mode(detail_msg_, LV_LABEL_LONG_WRAP);
    // No NAME row: the vessel name comes from AIS type 5 (static/voyage, a
    // multipart message not yet decoded) — see apps/ais/README.md. The MMSI is
    // the label everywhere instead of leaving an empty field waiting for data.
    lv_label_set_text(detail_label_,   "MMSI\nSOG\nCOG\nHDG\nSEEN");
    lv_label_set_text(detail_names_b_, "STAT\nRNG\nBRG");

    detail_canvas_ = lv_canvas_create(detail_box_);
    lv_canvas_set_buffer(detail_canvas_, detail_buf_.data(), kDetailRadarSize, kDetailRadarSize,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(detail_canvas_, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_size(detail_canvas_, kDetailRadarSize, kDetailRadarSize);
    lv_obj_align(detail_canvas_, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_remove_flag(detail_canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(detail_canvas_, LV_OBJ_FLAG_CLICKABLE);

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

    // Settings view.
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

void AisScreen::show_view(int screen) {
    last_view_ = screen;
    const bool list     = (screen == static_cast<int>(AisViewModel::Screen::List));
    const bool ppi      = (screen == static_cast<int>(AisViewModel::Screen::Radar));
    const bool detail   = (screen == static_cast<int>(AisViewModel::Screen::Detail));
    const bool settings = (screen == static_cast<int>(AisViewModel::Screen::Settings));

    const bool merc = vm_.map_mercator();
    if (list_view_)       lv_obj_set_flag(list_view_,       LV_OBJ_FLAG_HIDDEN, !list);
    if (ppi_canvas_)      lv_obj_set_flag(ppi_canvas_,      LV_OBJ_FLAG_HIDDEN, !ppi || merc);
    if (ppi_canvas_merc_) lv_obj_set_flag(ppi_canvas_merc_, LV_OBJ_FLAG_HIDDEN, !ppi || !merc);
    if (radar_left_)      lv_obj_set_flag(radar_left_,      LV_OBJ_FLAG_HIDDEN, !ppi);
    if (radar_right_)     lv_obj_set_flag(radar_right_,     LV_OBJ_FLAG_HIDDEN, !ppi);
    if (detail_box_)      lv_obj_set_flag(detail_box_,      LV_OBJ_FLAG_HIDDEN, !detail);
    if (settings_box_)    lv_obj_set_flag(settings_box_,    LV_OBJ_FLAG_HIDDEN, !settings);

    if (!ppi) {
        for (lv_obj_t* lbl : ppi_ring_labels_) {
            if (lbl) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

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

void AisScreen::update_header() {
    const auto& pal = view::palette(vm_.is_dark_mode());
    if (conn_dot_) {
        const bool ok = conn_state_ ? conn_state_() : false;
        lv_obj_set_style_bg_color(conn_dot_, ok ? pal.primary : lv_color_hex(0x888888), 0);
    }
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

void AisScreen::record_trails(const std::vector<Row>& rows) {
    const size_t cap = vm_.trail_len() > 0 ? static_cast<size_t>(vm_.trail_len()) : 600;
    std::unordered_set<std::string> live;
    live.reserve(rows.size());
    for (const auto& r : rows) {
        if (!r.has_pos) continue;
        live.insert(r.id);
        auto& hist = trails_[r.id];
        if (hist.empty() || hist.back().lat != r.pos.lat || hist.back().lon != r.pos.lon) {
            hist.push_back(r.pos);
        }
        while (hist.size() > cap) hist.pop_front();
    }
    for (auto it = trails_.begin(); it != trails_.end();) {
        it = (live.count(it->first) == 0) ? trails_.erase(it) : std::next(it);
    }
}

void AisScreen::render_scope(uint16_t* buf, int width, int height, lv_obj_t* canvas,
                             std::vector<lv_obj_t*>& ring_labels,
                             const std::vector<Row>& rows, int sel, bool show_others,
                             bool mercator) {
    if (!canvas) return;
    const int w = width, h = height;
    std::fill(buf, buf + static_cast<size_t>(w) * h, lv_color_to_u16(lv_color_black()));

    const int cx = w / 2, cy = h / 2;
    const int radius_px = (std::min(w, h) / 2) - 4;
    const double max_nm = static_cast<double>(vm_.range_nm());

    const uint16_t ring_col = lv_color_to_u16(lv_color_hex(0x223344));
    const uint16_t north_col = lv_color_to_u16(lv_color_hex(0x6688aa));
    const uint16_t home_col = lv_color_to_u16(lv_color_white());
    const uint16_t ship_col = lv_color_to_u16(view::palette(false).primary);
    const uint16_t sel_col = lv_color_to_u16(lv_color_hex(0xff9933));
    const uint16_t trail_col = lv_color_to_u16(lv_color_hex(0x4a7aa0));
    const uint16_t trail_sel_col = sel_col;

    if (mercator) {
        toolkit::map::MapViewport vp;
        vp.width = w;
        vp.height = h;
        vp.cx = cx;
        vp.cy = cy;
        vp.radius_px = radius_px;
        vp.home = config_.home;
        vp.range_nm = max_nm;
        vp.projection = toolkit::map::Projection::Mercator;
        toolkit::map::draw_base(buf, vp, base_map_, toolkit::map::MapStyle{});
        for (lv_obj_t* lbl : ring_labels)
            if (lbl) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        const int ring_px[3] = {radius_px / 3, (radius_px * 2) / 3, radius_px};
        for (int rp : ring_px) plot_ring(buf, w, h, cx, cy, rp, ring_col);

        for (int y = cy - radius_px; y < cy - radius_px + 8; ++y)
            if (y >= 0 && y < h) buf[y * w + cx] = north_col;

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
    plot_disc(buf, w, h, cx, cy, 2, home_col);

    auto project_pt = [&](const toolkit::geo::LatLon& p, int& dx, int& dy) -> bool {
        if (mercator) {
            if (!toolkit::geo::project_mercator(config_.home, p, max_nm, radius_px, dx, dy))
                return false;
            return cx + dx >= 0 && cx + dx < w && cy + dy >= 0 && cy + dy < h;
        }
        return toolkit::geo::project(config_.home, p, max_nm, radius_px, dx, dy);
    };

    const bool trails_on = vm_.show_trails();
    const std::string& sel_id = vm_.selected_id();

    if (trails_on) {
        for (const auto& r : rows) {
            if (!r.has_pos) continue;
            if (!show_others && (sel_id.empty() || r.id != sel_id)) continue;
            auto it = trails_.find(r.id);
            if (it == trails_.end() || it->second.size() < 2) continue;
            const uint16_t tc = (!sel_id.empty() && r.id == sel_id) ? trail_sel_col : trail_col;
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

    // One arrowhead per positioned vessel (pointing along its course): the
    // selected contact orange and larger, everyone else primary.
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        if (!r.has_pos) continue;
        if (!show_others && static_cast<int>(i) != sel) continue;
        int dx = 0, dy = 0;
        if (!project_pt(r.pos, dx, dy)) continue;
        const bool is_sel = (static_cast<int>(i) == sel);
        const bool has_dir = r.has_cog || r.has_hdg;
        const double dir = r.has_cog ? r.cog : static_cast<double>(r.hdg);
        const uint16_t col = is_sel ? sel_col : ship_col;
        plot_vessel(buf, w, h, cx + dx, cy + dy, has_dir, dir, col, is_sel ? 1.6f : 1.0f);
    }

    plot_disc(buf, w, h, 6, 6, 3,
              lv_color_to_u16(trails_on ? lv_color_hex(0x66aacc) : lv_color_hex(0x3a3a3a)));

    lv_obj_invalidate(canvas);
}

void AisScreen::update_ppi(const std::vector<Row>& rows) {
    const int sel = row_of(rows, vm_.selected_id());
    const bool mercator = vm_.map_mercator();

    lv_obj_t* canvas = mercator ? ppi_canvas_merc_ : ppi_canvas_;
    uint16_t* buf = mercator ? ppi_buf_merc_.data() : ppi_buf_.data();
    const int cw = mercator ? kPpiMercW : kPpiSize;
    const int ch = mercator ? kPpiMercH : kPpiSize;
    if (ppi_canvas_)      lv_obj_set_flag(ppi_canvas_,      LV_OBJ_FLAG_HIDDEN, mercator);
    if (ppi_canvas_merc_) lv_obj_set_flag(ppi_canvas_merc_, LV_OBJ_FLAG_HIDDEN, !mercator);

    render_scope(buf, cw, ch, canvas, ppi_ring_labels_, rows, sel,
                 /*show_others=*/true, mercator);

    const std::string& cur = vm_.cursor_id();
    const std::string& selh = vm_.selected_id();
    const size_t left_n = (rows.size() + 1) / 2;
    size_t li = 0, ri = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const bool on_left = (i < left_n);
        auto& pool = on_left ? radar_left_rows_ : radar_right_rows_;
        size_t& slot = on_left ? li : ri;
        if (slot >= pool.size()) continue;
        lv_obj_t* l = pool[slot++];

        const Row& r = rows[i];
        const std::string label = r.name.empty() ? r.id : r.name;
        const bool is_sel = (!selh.empty() && r.id == selh);
        const bool is_cur = (!is_sel && !cur.empty() && r.id == cur);
        lv_label_set_text(l, (is_cur ? "\xE2\x80\xBA" + label : label).c_str());

        if (is_sel) {
            lv_obj_set_style_bg_color(l, nav_color(r.nav_status), 0);
            lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(l, lv_color_black(), 0);
        } else {
            lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(l, nav_color(r.nav_status), 0);
        }
        lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t k = li; k < radar_left_rows_.size(); ++k)
        lv_obj_add_flag(radar_left_rows_[k], LV_OBJ_FLAG_HIDDEN);
    for (size_t k = ri; k < radar_right_rows_.size(); ++k)
        lv_obj_add_flag(radar_right_rows_[k], LV_OBJ_FLAG_HIDDEN);
}

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

void AisScreen::settings_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<AisScreen*>(lv_event_get_user_data(event));
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

void AisScreen::update_settings() {
    if (!settings_table_) return;
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

void AisScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<AisScreen*>(lv_timer_get_user_data(timer));
    if (self) self->tick();
}

void AisScreen::tick() {
    store_.sweep(vm_.ttl_seconds());
    auto rows = build_all_rows();
    record_trails(rows);
    apply_sort(rows);

    std::vector<std::string> order;
    order.reserve(rows.size());
    double max_nm = 0.0;
    for (const auto& r : rows) {
        order.push_back(r.id);
        if (r.has_pos && r.range_nm > max_nm) max_nm = r.range_nm;
    }
    vm_.set_visible_order(std::move(order));
    vm_.set_observed_max_nm(max_nm);

    const int screen = vm_.screen();
    if (screen != last_view_) {
        show_view(screen);
    }

    const int sel_row = row_of(rows, vm_.selected_id());
    const bool km = vm_.units_km();

    if (header_title_) {
        if (sel_row >= 0) {
            const Row& r = rows[static_cast<size_t>(sel_row)];
            const std::string label = r.name.empty() ? r.id : r.name;
            char sv[16] = "";
            switch (static_cast<AisViewModel::Sort>(vm_.sort_mode())) {
                case AisViewModel::Sort::Distance:
                    if (r.has_pos)
                        std::snprintf(sv, sizeof(sv), " %.0f%s", to_unit(r.range_nm, km),
                                      dist_unit(km));
                    break;
                case AisViewModel::Sort::Sog:
                    if (r.has_sog) std::snprintf(sv, sizeof(sv), " %.1fkt", r.sog);
                    break;
                case AisViewModel::Sort::Cog:
                    if (r.has_cog) std::snprintf(sv, sizeof(sv), " %.0f\xC2\xB0", r.cog);
                    break;
                case AisViewModel::Sort::Mmsi:
                default:
                    break;
            }
            char t[48];
            std::snprintf(t, sizeof(t), "%s%s", label.c_str(), sv);
            lv_label_set_text(header_title_, t);
        } else {
            lv_label_set_text(header_title_, "AIS");
        }
    }

    if (header_count_) {
        char info[32];
        switch (static_cast<AisViewModel::Screen>(vm_.screen())) {
            case AisViewModel::Screen::Radar:
                std::snprintf(info, sizeof(info), "%s%.0f%s", vm_.auto_range() ? "auto " : "",
                              to_unit(vm_.range_nm(), km), dist_unit(km));
                break;
            case AisViewModel::Screen::Detail:
                if (sel_row >= 0 && rows[static_cast<size_t>(sel_row)].has_pos)
                    std::snprintf(info, sizeof(info), "%.0f%s",
                                  to_unit(rows[static_cast<size_t>(sel_row)].range_nm, km),
                                  dist_unit(km));
                else
                    std::snprintf(info, sizeof(info), "detail");
                break;
            case AisViewModel::Screen::Settings:
                std::snprintf(info, sizeof(info), "settings");
                break;
            case AisViewModel::Screen::List:
            default:
                std::snprintf(info, sizeof(info), "%d shp", static_cast<int>(rows.size()));
                break;
        }
        lv_label_set_text(header_count_, info);
    }

    update_header();

    switch (static_cast<AisViewModel::Screen>(screen)) {
        case AisViewModel::Screen::List:   update_list(rows); break;
        case AisViewModel::Screen::Radar:  update_ppi(rows); break;
        case AisViewModel::Screen::Detail: update_detail(rows); break;
        case AisViewModel::Screen::Settings: update_settings(); break;
    }
}

} // namespace ais
