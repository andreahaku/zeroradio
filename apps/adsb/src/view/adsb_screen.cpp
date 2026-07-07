/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"
#include "adsb_screen_common.h"

#include <cstdio>
#include <string>

namespace adsb {

using namespace common;

namespace {

// Refresh period: the decode rate is low (tens of msgs/s), so a few Hz is plenty
// (much lighter than SDRTerminal's 30 fps waterfall).
constexpr uint32_t kTickPeriodMs = 300;

// Header height inside the content area.
constexpr int32_t kHeaderHeight = 18;

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
      ppi_buf_merc_(static_cast<size_t>(kPpiMercW) * kPpiMercH, 0u),
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
    font_tiny_  = assets().load_font("inter-regular.ttf", 10); // compact side rows

    // Base map for the Mercator radar mode (coastline + borders). Loaded once; if
    // the asset is missing the map degrades to an empty background (invalid).
    base_map_ = toolkit::map::VectorMap::load(
        assets().resolve("mapdata/world.rmap").string());

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

    // Wider Mercator map canvas, shown instead of the square PPI in map mode.
    // Separate fixed-size canvas (resizing one at runtime crashes SDL/Mesa).
    ppi_canvas_merc_ = lv_canvas_create(body_);
    lv_canvas_set_buffer(ppi_canvas_merc_, ppi_buf_merc_.data(), kPpiMercW, kPpiMercH,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(ppi_canvas_merc_, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_size(ppi_canvas_merc_, kPpiMercW, kPpiMercH);
    lv_obj_align(ppi_canvas_merc_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(ppi_canvas_merc_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ppi_canvas_merc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ppi_canvas_merc_, LV_OBJ_FLAG_HIDDEN);

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
    // colour-coded by category. A pool of reusable row labels per side, each row
    // carrying its own colour + background so the selected aircraft renders
    // inverted (category colour fill, black text) and the rest are coloured text
    // on a transparent background — they overlay the full-width map in map mode.
    // Compact rows (10px font, 11px step) so all 11 fit the short scope body.
    constexpr int kSideRows = 11; // == kPerSide in update_ppi()
    constexpr int kSideRowH = 11; // vertical step; keeps 11 rows within the body
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
            lv_obj_set_style_pad_hor(l, 2, 0);   // breathing room for the inverted pill
            lv_obj_set_style_radius(l, 2, 0);
            // Flush to the column's outer edge; right column also right-aligns text.
            lv_obj_align(l, right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, i * kSideRowH);
            if (right) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
            pool.push_back(l);
        }
    };
    make_rows(radar_left_, radar_left_rows_, false);
    make_rows(radar_right_, radar_right_rows_, true);

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

    // On the Radar screen, update_ppi swaps between the square radar and the wide
    // Mercator canvas; here we just honour the current mode so there's no
    // one-frame flash of the wrong canvas. The side callsign columns stay visible
    // in both modes (in map mode they overlay the full-width map, transparent bg).
    const bool merc = vm_.map_mercator();
    if (list_view_)     lv_obj_set_flag(list_view_,     LV_OBJ_FLAG_HIDDEN, !list);
    if (ppi_canvas_)      lv_obj_set_flag(ppi_canvas_,      LV_OBJ_FLAG_HIDDEN, !ppi || merc);
    if (ppi_canvas_merc_) lv_obj_set_flag(ppi_canvas_merc_, LV_OBJ_FLAG_HIDDEN, !ppi || !merc);
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
