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
#include <string>

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

// Text colour encoding the aircraft category (emergency overrides to red).
lv_color_t category_color(const std::string& category, bool emergency) {
    if (emergency) return lv_color_hex(0xff5050);
    if (category == "light")      return lv_color_hex(0x6fd66f); // green
    if (category == "small")      return lv_color_hex(0x66ccff); // cyan
    if (category == "large")      return lv_color_hex(0x4d9fff); // blue
    if (category == "heavy")      return lv_color_hex(0xffb347); // orange
    if (category == "rotorcraft") return lv_color_hex(0xc792ea); // purple
    return lv_color_hex(0xc8c8c8);                               // other/unknown: grey
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

// Max callsign labels overlaid on the PPI (one per visible aircraft, pooled).
constexpr size_t kMaxPpiLabels = 16;

// Length of the heading vector drawn from each aircraft dot, in pixels.
constexpr int kHeadingVectorPx = 10;

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
      ppi_buf_(static_cast<size_t>(kPpiSize) * kPpiSize, 0u) {
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

    // --- Header row: title | "N trk" | conn dot ---
    header_ = lv_obj_create(content);
    lv_obj_remove_style_all(header_);
    lv_obj_set_size(header_, LV_PCT(100), kHeaderHeight);
    lv_obj_clear_flag(header_, LV_OBJ_FLAG_SCROLLABLE);

    header_title_ = lv_label_create(header_);
    lv_label_set_text(header_title_, "ADSB");
    lv_obj_set_style_text_font(header_title_, font_mono_ ? font_mono_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(header_title_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(header_title_, LV_ALIGN_LEFT_MID, 4, 0);

    header_count_ = lv_label_create(header_);
    lv_label_set_text(header_count_, "0 trk");
    lv_obj_set_style_text_font(header_count_, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(header_count_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(header_count_, LV_ALIGN_CENTER, -8, 0);

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

    // List view: a 5-column table — callsign / altitude / speed / track / distance
    // — with a header row. Row 0 is the header, so data rows are 1..N.
    list_table_ = lv_table_create(body_);
    lv_obj_set_size(list_table_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(list_table_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_table_set_column_count(list_table_, 5);
    lv_table_set_column_width(list_table_, 0, 92); // CALL
    lv_table_set_column_width(list_table_, 1, 58); // ALT
    lv_table_set_column_width(list_table_, 2, 48); // SPD
    lv_table_set_column_width(list_table_, 3, 46); // TRK
    lv_table_set_column_width(list_table_, 4, 60); // DST
    lv_obj_set_style_pad_all(list_table_, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_width(list_table_, 0, 0);
    if (font_small_) {
        lv_obj_set_style_text_font(list_table_, font_small_, LV_PART_ITEMS);
    }
    lv_table_set_cell_value(list_table_, 0, 0, "CALL");
    lv_table_set_cell_value(list_table_, 0, 1, "ALT");
    lv_table_set_cell_value(list_table_, 0, 2, "SPD");
    lv_table_set_cell_value(list_table_, 0, 3, "TRK");
    lv_table_set_cell_value(list_table_, 0, 4, "DST");
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

    // Callsign label pool overlaid on the PPI. Children of body_, centred like the
    // canvas, so a (dx,dy) offset from the centre lands on the matching dot. The
    // PPI background is always black, so the labels use a fixed light colour.
    ppi_labels_.reserve(kMaxPpiLabels);
    for (size_t i = 0; i < kMaxPpiLabels; ++i) {
        lv_obj_t* lbl = lv_label_create(body_);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xc8d6c8), 0);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        ppi_labels_.push_back(lbl);
    }

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

    // Detail view (all fields of the selected aircraft as label rows).
    detail_box_ = lv_obj_create(body_);
    lv_obj_remove_style_all(detail_box_);
    lv_obj_set_size(detail_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(detail_box_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(detail_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(detail_box_, 4, 0);
    detail_label_ = lv_label_create(detail_box_);
    lv_label_set_text(detail_label_, "");
    lv_obj_set_style_text_font(detail_label_, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(detail_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(detail_label_, LV_ALIGN_TOP_LEFT, 0, 0);

    // Settings view (static for now: a hint + the quit key).
    settings_box_ = lv_obj_create(body_);
    lv_obj_remove_style_all(settings_box_);
    lv_obj_set_size(settings_box_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(settings_box_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(settings_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(settings_box_, 6, 0);
    settings_label_ = lv_label_create(settings_box_);
    lv_label_set_text(settings_label_,
                      "SETTINGS\n\nHome  45.46, 9.19\nUnits  NM\nTTL    30 s\n\n8: quit");
    lv_obj_set_style_text_font(settings_label_, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(settings_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(settings_label_, LV_ALIGN_TOP_LEFT, 0, 0);

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

    if (list_table_)    lv_obj_set_flag(list_table_,    LV_OBJ_FLAG_HIDDEN, !list);
    if (ppi_canvas_)    lv_obj_set_flag(ppi_canvas_,    LV_OBJ_FLAG_HIDDEN, !ppi);
    if (detail_box_)    lv_obj_set_flag(detail_box_,    LV_OBJ_FLAG_HIDDEN, !detail);
    if (settings_box_)  lv_obj_set_flag(settings_box_,  LV_OBJ_FLAG_HIDDEN, !settings);

    // The PPI callsign labels only belong to the Radar view; hide them otherwise
    // (update_ppi re-shows the ones it uses on each Radar tick).
    if (!ppi) {
        for (lv_obj_t* lbl : ppi_labels_) {
            if (lbl) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        }
        for (lv_obj_t* lbl : ppi_ring_labels_) {
            if (lbl) lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

std::vector<AdsbScreen::Row> AdsbScreen::build_rows() {
    auto snapshot = store_.snapshot();
    std::vector<Row> rows;
    rows.reserve(snapshot.size());

    for (const auto& e : snapshot) {
        Row r;
        r.hex = e.id;
        r.flight = field_str(e, "flight");
        r.has_pos = e.has_pos;
        r.pos = e.pos;
        r.alt = field_long(e, "alt", r.has_alt);
        r.on_ground = !field_str(e, "on_ground").empty();
        r.gs = field_long(e, "gs", r.has_gs);
        r.track = field_long(e, "track", r.has_track);
        r.squawk = field_str(e, "squawk");
        r.category = field_str(e, "category");
        r.emergency = field_str(e, "emergency") == "1";
        bool has_seen = false;
        r.seen = field_long(e, "seen", has_seen);
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

    return rows;
}

int AdsbScreen::selected_row(const std::vector<Row>& rows) const {
    if (rows.empty()) return -1;
    const std::string& hex = vm_.selected_hex();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].hex == hex) return static_cast<int>(i);
    }
    return 0; // selection not on screen -> fall back to the first row
}

void AdsbScreen::update_header(int track_count, int signal_quality) {
    if (header_count_) {
        // "16 trk  A150NM" — the range readout shows the current outer ring, with
        // a leading "A" when it is auto-fitting the traffic.
        lv_label_set_text_fmt(header_count_, "%d trk  %s%dNM",
                              track_count, vm_.auto_range() ? "A" : "", vm_.range_nm());
    }
    if (sig_bar_) {
        lv_bar_set_value(sig_bar_, signal_quality, LV_ANIM_OFF);
    }
    if (conn_dot_) {
        const bool ok = conn_state_ ? conn_state_() : false;
        lv_obj_set_style_bg_color(conn_dot_,
                                  ok ? view::palette(false).primary
                                     : lv_color_hex(0x888888),
                                  0);
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

    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel) {
        // Strong highlight: fill the selected row with the accent colour.
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(false).primary;
        fd->opa = LV_OPA_COVER;
        return;
    }
    if (type == LV_DRAW_TASK_TYPE_LABEL) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        if (row == 0) {
            return; // header row keeps the theme colour
        }
        if (is_sel) {
            ld->color = lv_color_black();    // contrast against the accent fill
        } else if (row < self->list_row_colors_.size()) {
            ld->color = self->list_row_colors_[row];
        }
    }
}

void AdsbScreen::update_list(const std::vector<Row>& rows) {
    if (!list_table_) {
        return;
    }

    // Row 0 is the header; data rows are 1..N.
    lv_table_set_row_count(list_table_, static_cast<uint32_t>(rows.size()) + 1);

    // Per-row colours (index 0 = header, kept default).
    list_row_colors_.assign(rows.size() + 1, lv_color_white());

    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        const uint32_t row = static_cast<uint32_t>(i) + 1;
        list_row_colors_[row] = category_color(r.category, r.emergency);
        char alt_buf[12];
        char gs_buf[12];
        char trk_buf[8];
        char rng_buf[12];
        if (r.has_alt)        std::snprintf(alt_buf, sizeof(alt_buf), "%ld", r.alt);
        else if (r.on_ground) std::snprintf(alt_buf, sizeof(alt_buf), "grnd");
        else                  std::snprintf(alt_buf, sizeof(alt_buf), "-");
        if (r.has_gs)    std::snprintf(gs_buf, sizeof(gs_buf), "%ld", r.gs);
        else             std::snprintf(gs_buf, sizeof(gs_buf), "-");
        if (r.has_track) std::snprintf(trk_buf, sizeof(trk_buf), "%ld", r.track);
        else             std::snprintf(trk_buf, sizeof(trk_buf), "-");
        if (r.has_pos)   std::snprintf(rng_buf, sizeof(rng_buf), "%.0f", r.range_nm);
        else             std::snprintf(rng_buf, sizeof(rng_buf), "-");

        const std::string call =
            (r.emergency ? std::string("!") : std::string()) +
            (r.flight.empty() ? r.hex : r.flight);
        lv_table_set_cell_value(list_table_, row, 0, call.c_str());
        lv_table_set_cell_value(list_table_, row, 1, alt_buf);
        lv_table_set_cell_value(list_table_, row, 2, gs_buf);
        lv_table_set_cell_value(list_table_, row, 3, trk_buf);
        lv_table_set_cell_value(list_table_, row, 4, rng_buf);
    }

    // Highlight the selected row (by hex). With nothing selected, park the
    // highlight on the header row (row 0) so no aircraft looks selected.
    const int sel = selected_row(rows);
    const bool has_sel = (sel >= 0 && !vm_.selected_hex().empty());
    list_sel_row_ = has_sel ? sel + 1 : -1; // drives the strong row highlight
    lv_table_set_selected_cell(list_table_, has_sel ? static_cast<uint16_t>(sel + 1) : 0, 0);
}

void AdsbScreen::update_ppi(const std::vector<Row>& rows) {
    if (!ppi_canvas_) {
        return;
    }

    uint16_t* buf = ppi_buf_.data();
    const int w = kPpiSize;
    const int h = kPpiSize;
    std::fill(ppi_buf_.begin(), ppi_buf_.end(), lv_color_to_u16(lv_color_black()));

    const int cx = w / 2;
    const int cy = h / 2;
    const int radius_px = (std::min(w, h) / 2) - 4;

    const uint16_t ring_col = lv_color_to_u16(lv_color_hex(0x224422));
    const uint16_t north_col = lv_color_to_u16(lv_color_hex(0x66aa66));
    const uint16_t home_col = lv_color_to_u16(lv_color_white());
    const uint16_t ac_col = lv_color_to_u16(view::palette(false).primary);
    const uint16_t emg_col = lv_color_to_u16(lv_color_hex(0xff4040));

    // Concentric range rings (1/3, 2/3, full).
    plot_ring(buf, w, h, cx, cy, radius_px / 3, ring_col);
    plot_ring(buf, w, h, cx, cy, (radius_px * 2) / 3, ring_col);
    plot_ring(buf, w, h, cx, cy, radius_px, ring_col);

    // North tick: a short line straight up from the centre.
    for (int y = cy - radius_px; y < cy - radius_px + 8; ++y) {
        if (y >= 0 && y < h) buf[y * w + cx] = north_col;
    }

    // Home dot at the centre.
    plot_disc(buf, w, h, cx, cy, 2, home_col);

    // Range-ring scale labels (NM) along the north axis: one per ring so the
    // scale is readable (the rings sit at 1/3, 2/3, full of the current range).
    const int ring_nm = vm_.range_nm();
    const int ring_px[3] = {radius_px / 3, (radius_px * 2) / 3, radius_px};
    for (size_t i = 0; i < ppi_ring_labels_.size(); ++i) {
        lv_obj_t* lbl = ppi_ring_labels_[i];
        if (!lbl) continue;
        lv_label_set_text_fmt(lbl, "%d", ring_nm * (static_cast<int>(i) + 1) / 3);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 4, -ring_px[i] + 6);
    }

    // One dot per positioned aircraft within range, with a heading vector. Only
    // the *selected* contact (and any emergency) gets a callsign label — labelling
    // every aircraft overlaps illegibly when the sky is busy. The selection (same
    // sorted order as the list) is shown with a larger dot ringed in white; scroll
    // it with the list keys to read each callsign in turn.
    const int sel = vm_.selected_hex().empty() ? -1 : selected_row(rows);
    const bool labels_on = vm_.show_labels();

    const uint16_t vec_col = lv_color_to_u16(lv_color_hex(0x88cc88));
    const uint16_t sel_col = lv_color_to_u16(lv_color_white());
    const double max_nm = static_cast<double>(vm_.range_nm());
    size_t label_i = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        if (!r.has_pos) continue;
        int dx = 0;
        int dy = 0;
        if (!toolkit::geo::project(config_.home, r.pos, max_nm, radius_px, dx, dy)) {
            continue;
        }
        const int px = cx + dx;
        const int py = cy + dy;
        const bool is_sel = (static_cast<int>(i) == sel);
        const uint16_t col = r.emergency ? emg_col : ac_col;

        // Heading vector: a short line from the dot along the reported track
        // (north-up: 0 deg points up, 90 deg points right).
        if (r.has_track) {
            const double trk = static_cast<double>(r.track) * 3.14159265358979323846 / 180.0;
            const int ex = px + static_cast<int>(std::lround(kHeadingVectorPx * std::sin(trk)));
            const int ey = py - static_cast<int>(std::lround(kHeadingVectorPx * std::cos(trk)));
            plot_line(buf, w, h, px, py, ex, ey, r.emergency ? emg_col : vec_col);
        }

        if (is_sel) {
            // Selected contact: brighter, larger, ringed so it stands out.
            plot_disc(buf, w, h, px, py, 3, col);
            plot_ring(buf, w, h, px, py, 5, sel_col);
        } else {
            plot_disc(buf, w, h, px, py, 2, col);
        }

        // Label the selected contact and any emergency (when labels are on), with
        // the callsign and (for the selection) its altitude on a second line.
        if (labels_on && (is_sel || r.emergency) && label_i < ppi_labels_.size()) {
            lv_obj_t* lbl = ppi_labels_[label_i++];
            std::string txt = r.flight.empty() ? r.hex : r.flight;
            if (is_sel) {
                if (r.has_alt) {
                    char altbuf[16];
                    std::snprintf(altbuf, sizeof(altbuf), "\n%ldft", r.alt);
                    txt += altbuf;
                } else if (r.on_ground) {
                    txt += "\ngrnd";
                }
            }
            lv_label_set_text(lbl, txt.c_str());
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_style_text_color(lbl, r.emergency ? lv_color_hex(0xff6060)
                                                         : lv_color_hex(0xffffff), 0);
            lv_obj_remove_flag(lbl, LV_OBJ_FLAG_HIDDEN);
            // Labels are centred on body_, which shares the canvas centre, so the
            // (dx,dy) projection offset maps straight onto the dot. Flip the label
            // to the left of the dot in the right half so it stays over the scope.
            const int32_t off_x = (dx > 0) ? (dx - 28) : (dx + 5);
            lv_obj_align(lbl, LV_ALIGN_CENTER, off_x, dy - 7);
        }
    }

    // Hide any pool labels left unused this tick.
    for (size_t i = label_i; i < ppi_labels_.size(); ++i) {
        if (ppi_labels_[i]) lv_obj_add_flag(ppi_labels_[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_invalidate(ppi_canvas_);
}

void AdsbScreen::update_detail(const std::vector<Row>& rows) {
    if (!detail_label_) {
        return;
    }
    const int sel = vm_.selected_hex().empty() ? -1 : selected_row(rows);
    if (rows.empty() || sel < 0) {
        lv_label_set_text(detail_label_, "(no aircraft selected)\n\nList: \xE2\x86\x91/\xE2\x86\x93 + select");
        return;
    }
    const Row& r = rows[static_cast<size_t>(sel)];

    // Build each value into a named string first to avoid dangling temporaries.
    const std::string alt_s = r.has_alt ? (std::to_string(r.alt) + " ft")
                              : r.on_ground ? std::string("ground")
                                            : std::string("-");
    const std::string gs_s  = r.has_gs ? (std::to_string(r.gs) + " kt") : std::string("-");
    const std::string trk_s = r.has_track ? (std::to_string(r.track) + " deg") : std::string("-");
    const std::string rng_s = r.has_pos
                                  ? (std::to_string(static_cast<long>(r.range_nm)) + " NM")
                                  : std::string("-");
    const std::string brg_s = r.has_pos
                                  ? (std::to_string(static_cast<long>(r.bearing_deg)) + " deg")
                                  : std::string("-");

    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "HEX  %s\n"
                  "FLT  %s\n"
                  "ALT  %s\n"
                  "GS   %s\n"
                  "TRK  %s\n"
                  "SQK  %s%s\n"
                  "CAT  %s\n"
                  "RNG  %s\n"
                  "BRG  %s\n"
                  "SEEN %lds",
                  r.hex.c_str(),
                  r.flight.empty() ? "-" : r.flight.c_str(),
                  alt_s.c_str(),
                  gs_s.c_str(),
                  trk_s.c_str(),
                  r.squawk.empty() ? "-" : r.squawk.c_str(),
                  r.emergency ? "  !EMERGENCY" : "",
                  r.category.empty() ? "-" : r.category.c_str(),
                  rng_s.c_str(),
                  brg_s.c_str(),
                  r.seen);
    lv_label_set_text(detail_label_, buf);
}

void AdsbScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<AdsbScreen*>(lv_timer_get_user_data(timer));
    if (self) {
        self->tick();
    }
}

void AdsbScreen::tick() {
    // Drop stale entries, then snapshot + sort.
    store_.sweep(config_.ttl_seconds);
    const auto rows = build_rows();

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

    update_header(static_cast<int>(rows.size()), signal_quality);

    switch (static_cast<AdsbViewModel::Screen>(screen)) {
        case AdsbViewModel::Screen::List:   update_list(rows); break;
        case AdsbViewModel::Screen::Radar:  update_ppi(rows); break;
        case AdsbViewModel::Screen::Detail: update_detail(rows); break;
        case AdsbViewModel::Screen::Settings: break; // static
    }
}

} // namespace adsb
