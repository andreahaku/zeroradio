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
    lv_obj_align(header_count_, LV_ALIGN_CENTER, 0, 0);

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

    // List view (a single-column table; row text = "CALLSIGN ALT GS RNG").
    list_table_ = lv_table_create(body_);
    lv_obj_set_size(list_table_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(list_table_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_table_set_column_count(list_table_, 1);
    lv_table_set_column_width(list_table_, 0, view::kScreenWidth);
    lv_obj_set_style_pad_all(list_table_, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_width(list_table_, 0, 0);
    if (font_mono_) {
        lv_obj_set_style_text_font(list_table_, font_mono_, LV_PART_ITEMS);
    }
    lv_obj_remove_flag(list_table_, LV_OBJ_FLAG_CLICKABLE);

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

    // Start on the viewmodel's current view.
    show_view(vm_.view_mode());

    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);
}

void AdsbScreen::show_view(int view_mode) {
    last_view_ = view_mode;
    const bool list   = (view_mode == static_cast<int>(AdsbViewModel::View::List));
    const bool ppi    = (view_mode == static_cast<int>(AdsbViewModel::View::PPI));
    const bool detail = (view_mode == static_cast<int>(AdsbViewModel::View::Detail));

    if (list_table_)  lv_obj_set_flag(list_table_,  LV_OBJ_FLAG_HIDDEN, !list);
    if (ppi_canvas_)  lv_obj_set_flag(ppi_canvas_,  LV_OBJ_FLAG_HIDDEN, !ppi);
    if (detail_box_)  lv_obj_set_flag(detail_box_,  LV_OBJ_FLAG_HIDDEN, !detail);

    // The PPI callsign labels only belong to the PPI view; hide them otherwise
    // (update_ppi re-shows the ones it uses on each PPI tick).
    if (!ppi) {
        for (lv_obj_t* lbl : ppi_labels_) {
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
        if (r.has_pos) {
            r.range_nm = toolkit::geo::range_nm(config_.home, r.pos);
            r.bearing_deg = toolkit::geo::bearing_deg(config_.home, r.pos);
        }
        rows.push_back(std::move(r));
    }

    // Sort per the viewmodel. Position-less aircraft sort last for range.
    const int sort = vm_.sort_mode();
    if (sort == static_cast<int>(AdsbViewModel::Sort::Range)) {
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.has_pos != b.has_pos) return a.has_pos; // positioned first
            return a.range_nm < b.range_nm;
        });
    } else if (sort == static_cast<int>(AdsbViewModel::Sort::Alt)) {
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.has_alt != b.has_alt) return a.has_alt;
            return a.alt > b.alt; // highest first
        });
    } else { // Callsign
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            const std::string& ka = a.flight.empty() ? a.hex : a.flight;
            const std::string& kb = b.flight.empty() ? b.hex : b.flight;
            return ka < kb;
        });
    }

    return rows;
}

void AdsbScreen::update_header(int track_count) {
    if (header_count_) {
        lv_label_set_text_fmt(header_count_, "%d trk", track_count);
    }
    if (conn_dot_) {
        const bool ok = conn_state_ ? conn_state_() : false;
        lv_obj_set_style_bg_color(conn_dot_,
                                  ok ? view::palette(false).primary
                                     : lv_color_hex(0x888888),
                                  0);
    }
}

void AdsbScreen::update_list(const std::vector<Row>& rows) {
    if (!list_table_) {
        return;
    }

    lv_table_set_row_count(list_table_, rows.empty() ? 1 : static_cast<uint32_t>(rows.size()));
    if (rows.empty()) {
        lv_table_set_cell_value(list_table_, 0, 0, "  (no aircraft)");
        return;
    }

    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        char alt_buf[12];
        char gs_buf[12];
        char rng_buf[12];
        if (r.has_alt)        std::snprintf(alt_buf, sizeof(alt_buf), "%ldft", r.alt);
        else if (r.on_ground) std::snprintf(alt_buf, sizeof(alt_buf), "grnd");
        else                  std::snprintf(alt_buf, sizeof(alt_buf), "-");
        if (r.has_gs)  std::snprintf(gs_buf, sizeof(gs_buf), "%ldkt", r.gs);
        else           std::snprintf(gs_buf, sizeof(gs_buf), "-");
        if (r.has_pos) std::snprintf(rng_buf, sizeof(rng_buf), "%.0fNM", r.range_nm);
        else           std::snprintf(rng_buf, sizeof(rng_buf), "-");

        const std::string call = r.flight.empty() ? r.hex : r.flight;
        char line[64];
        std::snprintf(line, sizeof(line), "%s%-8s %-7s %-6s %s",
                      r.emergency ? "! " : "  ",
                      call.c_str(), alt_buf, gs_buf, rng_buf);
        lv_table_set_cell_value(list_table_, static_cast<uint32_t>(i), 0, line);
    }

    // Highlight the selected row (clamped to the row count).
    int sel = vm_.selected_index();
    if (sel >= static_cast<int>(rows.size())) sel = static_cast<int>(rows.size()) - 1;
    if (sel < 0) sel = 0;
    lv_table_set_selected_cell(list_table_, static_cast<uint16_t>(sel), 0);
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

    // One dot per positioned aircraft within range, with a heading vector. Only
    // the *selected* contact (and any emergency) gets a callsign label — labelling
    // every aircraft overlaps illegibly when the sky is busy. The selection (same
    // sorted order as the list) is shown with a larger dot ringed in white; scroll
    // it with the list keys to read each callsign in turn.
    int sel = vm_.selected_index();
    if (sel >= static_cast<int>(rows.size())) sel = static_cast<int>(rows.size()) - 1;
    if (sel < 0) sel = 0;

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

        // Label only the selected contact and any emergency.
        if ((is_sel || r.emergency) && label_i < ppi_labels_.size()) {
            lv_obj_t* lbl = ppi_labels_[label_i++];
            const std::string call = r.flight.empty() ? r.hex : r.flight;
            lv_label_set_text(lbl, call.c_str());
            lv_obj_set_style_text_color(lbl, r.emergency ? lv_color_hex(0xff6060)
                                                         : lv_color_hex(0xffffff), 0);
            lv_obj_remove_flag(lbl, LV_OBJ_FLAG_HIDDEN);
            // Labels are centred on body_, which shares the canvas centre, so the
            // (dx,dy) projection offset maps straight onto the dot.
            lv_obj_align(lbl, LV_ALIGN_CENTER, dx + 5, dy - 7);
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
    if (rows.empty()) {
        lv_label_set_text(detail_label_, "(no aircraft selected)");
        return;
    }

    int sel = vm_.selected_index();
    if (sel >= static_cast<int>(rows.size())) sel = static_cast<int>(rows.size()) - 1;
    if (sel < 0) sel = 0;
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

    // Let the NavBar's "next" action clamp against the live row count.
    vm_.set_visible_count(static_cast<int>(rows.size()));

    const int view_mode = vm_.view_mode();
    if (view_mode != last_view_) {
        show_view(view_mode);
    }

    update_header(static_cast<int>(rows.size()));

    if (view_mode == static_cast<int>(AdsbViewModel::View::List)) {
        update_list(rows);
    } else if (view_mode == static_cast<int>(AdsbViewModel::View::PPI)) {
        update_ppi(rows);
    } else {
        update_detail(rows);
    }

    // Status line: "ADSB  N trk  MOCK".
    char status[48];
    std::snprintf(status, sizeof(status), "ADSB  %d trk  MOCK", static_cast<int>(rows.size()));
    vm_.set_subtitle(status);
}

} // namespace adsb
