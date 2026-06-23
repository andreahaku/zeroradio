/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "geo.h"
#include "linux_input.h"
#include "map_renderer.h"
#include "theme.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

namespace meshtastic {
namespace {

constexpr uint32_t kTickPeriodMs = 300;
constexpr double kNodeTtlSeconds = 3600.0; // nodes beacon rarely; keep an hour

// NODES table columns (fit the 320px content width).
constexpr int32_t kColW[4] = {116, 64, 54, 62};
const char* const kColTitle[4] = {"SHORT", "SNR", "HOP", "AGE"};
constexpr int32_t kHeaderH = 15;

std::string field_of(const toolkit::Entity& e, const char* key) {
    const auto it = e.fields.find(key);
    return it != e.fields.end() ? it->second : std::string{};
}

// Canned quick-reply presets (CHATS key 6). Kept short for the mesh; the picker
// sends the chosen one on the current conversation by its list number.
const char* const kCanned[] = {
    "QRV (ready)",
    "On my way",
    "Roger",
    "Standby",
    "At the meeting point",
    "Need help",
    "73",
};
constexpr int kCannedCount = static_cast<int>(sizeof(kCanned) / sizeof(kCanned[0]));

// MAP (PPI) geometry. Content area is 320x110 (title + nav bars take 30 each), so
// a 106px square canvas fits with a hair of margin; the flanking side columns
// carry the colour-coded node names.
constexpr int kMapSize = 106;
// The Mercator map view isn't bound to a circle, so it spreads to the full
// screen width (same height); the flanking short-name columns stay anchored to
// the screen edges and overlay the map with a transparent background (the
// selected node still renders inverted).
constexpr int kMapMercW = 320;
constexpr int kMapMercH = 106;

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

// Fill a disc of radius r at (cx, cy).
void plot_disc(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= w || y < 0 || y >= h) continue;
            buf[y * w + x] = color;
        }
    }
}

// Thin ring (midpoint circle) of radius r around (cx, cy).
void plot_ring(uint16_t* buf, int w, int h, int cx, int cy, int r, uint16_t color) {
    int x = r, y = 0, err = 1 - r;
    const auto put = [&](int px, int py) {
        if (px >= 0 && px < w && py >= 0 && py < h) buf[py * w + px] = color;
    };
    while (x >= y) {
        put(cx + x, cy + y); put(cx - x, cy + y);
        put(cx + x, cy - y); put(cx - x, cy - y);
        put(cx + y, cy + x); put(cx - y, cy + x);
        put(cx + y, cy - x); put(cx - y, cy - x);
        ++y;
        if (err < 0) { err += 2 * y + 1; }
        else { --x; err += 2 * (y - x) + 1; }
    }
}

} // namespace

MeshtasticScreen::MeshtasticScreen(
    MeshtasticViewModel& vm,
    app::AssetManager& assets,
    toolkit::EntityStore& store,
    MessageLog& messages,
    ChannelTable& channels,
    const MeshtasticClientSource& source,
    std::function<uint32_t(const std::string&, uint32_t, uint8_t)> on_send)
    : BaseScreen(vm, vm, assets),
      vm_(vm),
      store_(store),
      messages_(messages),
      channels_(channels),
      source_(source),
      on_send_(std::move(on_send)) {
    init();
}

MeshtasticScreen::~MeshtasticScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void MeshtasticScreen::build_content(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    font_big_        = assets().load_font("inter-semibold.ttf", 22);
    font_small_      = assets().load_font("inter-regular.ttf", 12);
    font_small_bold_ = assets().load_font("inter-bold.ttf", 12);
    const lv_font_t* fb = font_big_ ? font_big_ : &lv_font_montserrat_12;
    const lv_font_t* fs = font_small_ ? font_small_ : &lv_font_montserrat_12;

    // Base map for the Mercator view (coastline + borders). Loaded once; if the
    // asset is missing the map degrades to an empty background (valid()==false).
    base_map_ = toolkit::map::VectorMap::load(
        assets().resolve("mapdata/adriatic.rmap").string());

    // Placeholder big label, centred — shown on every page except NODES.
    view_label_ = lv_label_create(content);
    lv_label_set_text(view_label_, "CHATS");
    lv_obj_set_style_text_font(view_label_, fb, 0);
    reactive::bind_theme(view_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(view_label_, LV_ALIGN_CENTER, 0, -6);

    hint_label_ = lv_label_create(content);
    lv_label_set_text(hint_label_, "press 4 to switch view  -  ESC to quit");
    lv_obj_set_style_text_font(hint_label_, fs, 0);
    lv_obj_set_style_text_color(hint_label_, lv_color_hex(0x888888), 0);
    lv_obj_align(hint_label_, LV_ALIGN_CENTER, 0, 16);

    // CHATS feed: a recolour multi-line label (sender short name coloured, body in
    // theme text). Hidden until the CHATS page. Self in accent green, peers info-blue.
    chats_label_ = lv_label_create(content);
    lv_label_set_recolor(chats_label_, true);
    lv_label_set_long_mode(chats_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(chats_label_, "");
    lv_obj_set_width(chats_label_, LV_PCT(100));
    lv_obj_set_style_text_font(chats_label_, fs, 0);
    reactive::bind_theme(chats_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(chats_label_, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_obj_add_flag(chats_label_, LV_OBJ_FLAG_HIDDEN);

    // CHATS compose input row, pinned at the bottom; shown only while composing.
    compose_row_ = lv_label_create(content);
    lv_label_set_long_mode(compose_row_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(compose_row_, "> _");
    lv_obj_set_width(compose_row_, LV_PCT(100));
    lv_obj_set_style_text_font(compose_row_, fs, 0);
    lv_obj_set_style_bg_color(compose_row_, view::palette(vm_.is_dark_mode()).surface, 0);
    lv_obj_set_style_bg_opa(compose_row_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(compose_row_, view::palette(vm_.is_dark_mode()).primary, 0);
    lv_obj_set_style_border_width(compose_row_, 1, 0);
    lv_obj_set_style_pad_all(compose_row_, 3, 0);
    reactive::bind_theme(compose_row_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(compose_row_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(compose_row_, LV_OBJ_FLAG_HIDDEN);

    // CHATS canned-message overlay: a numbered list; pressing a digit sends that
    // preset. Centred card, shown only while the picker is open.
    canned_box_ = lv_label_create(content);
    lv_label_set_recolor(canned_box_, true);
    lv_label_set_text(canned_box_, "");
    lv_obj_set_style_text_font(canned_box_, fs, 0);
    lv_obj_set_style_bg_color(canned_box_, view::palette(vm_.is_dark_mode()).surface, 0);
    lv_obj_set_style_bg_opa(canned_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(canned_box_, view::palette(vm_.is_dark_mode()).primary, 0);
    lv_obj_set_style_border_width(canned_box_, 1, 0);
    lv_obj_set_style_radius(canned_box_, 4, 0);
    lv_obj_set_style_pad_all(canned_box_, 5, 0);
    reactive::bind_theme(canned_box_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(canned_box_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(canned_box_, LV_OBJ_FLAG_HIDDEN);

    // NODES view: a fixed column header strip + a themed table. Hidden by default.
    nodes_view_ = lv_obj_create(content);
    lv_obj_remove_style_all(nodes_view_);
    lv_obj_set_size(nodes_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(nodes_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(nodes_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(nodes_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(nodes_view_, 0, 0);
    lv_obj_set_style_pad_row(nodes_view_, 0, 0);
    lv_obj_add_flag(nodes_view_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = lv_obj_create(nodes_view_);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kHeaderH);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    int32_t hx = 0;
    for (int c = 0; c < 4; ++c) {
        lv_obj_t* h = lv_label_create(header);
        lv_label_set_text(h, kColTitle[c]);
        lv_obj_set_style_text_font(h, fs, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0x888888), 0);
        lv_obj_align(h, LV_ALIGN_LEFT_MID, hx + 4, 0);
        hx += kColW[c];
    }

    nodes_table_ = lv_table_create(nodes_view_);
    lv_obj_set_width(nodes_table_, LV_PCT(100));
    lv_obj_set_flex_grow(nodes_table_, 1);
    lv_table_set_column_count(nodes_table_, 4);
    for (int c = 0; c < 4; ++c) lv_table_set_column_width(nodes_table_, c, kColW[c]);
    lv_obj_set_style_pad_all(nodes_table_, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_width(nodes_table_, 0, 0);
    lv_obj_set_style_text_font(nodes_table_, fs, LV_PART_ITEMS);
    lv_obj_remove_flag(nodes_table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(nodes_table_, nodes_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(nodes_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    // NODE DETAIL panel: a single recolour label that lists all fields of the
    // selected node. Hidden by default; shown when detail sub-screen is open.
    node_detail_ = lv_label_create(content);
    lv_label_set_recolor(node_detail_, true);
    lv_label_set_long_mode(node_detail_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(node_detail_, LV_PCT(100));
    lv_obj_set_style_text_font(node_detail_, fs, 0);
    reactive::bind_theme(node_detail_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(node_detail_, LV_ALIGN_TOP_LEFT, 4, 2);
    lv_label_set_text(node_detail_, "");
    lv_obj_add_flag(node_detail_, LV_OBJ_FLAG_HIDDEN);

    // SETTINGS view: a 2-column lv_table (name | value) with a green cursor band,
    // plus a compose-style text row for Long/Short name editing.
    settings_view_ = lv_obj_create(content);
    lv_obj_remove_style_all(settings_view_);
    lv_obj_set_size(settings_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(settings_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(settings_view_, 0, 0);
    lv_obj_add_flag(settings_view_, LV_OBJ_FLAG_HIDDEN);

    settings_table_ = lv_table_create(settings_view_);
    lv_obj_set_size(settings_table_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(settings_table_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_table_set_column_count(settings_table_, 2);
    lv_table_set_column_width(settings_table_, 0, 150);
    lv_table_set_column_width(settings_table_, 1, 158);
    lv_obj_set_style_pad_ver(settings_table_, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(settings_table_, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_width(settings_table_, 0, 0);
    lv_obj_set_style_text_font(settings_table_, fs, LV_PART_ITEMS);
    lv_obj_remove_flag(settings_table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings_table_, settings_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(settings_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    // Text-edit row (shown only while editing a Long/Short name field).
    settings_edit_row_ = lv_label_create(settings_view_);
    lv_label_set_long_mode(settings_edit_row_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(settings_edit_row_, "> _");
    lv_obj_set_width(settings_edit_row_, LV_PCT(100));
    lv_obj_set_style_text_font(settings_edit_row_, fs, 0);
    lv_obj_set_style_bg_color(settings_edit_row_, view::palette(vm_.is_dark_mode()).surface, 0);
    lv_obj_set_style_bg_opa(settings_edit_row_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(settings_edit_row_, view::palette(vm_.is_dark_mode()).primary, 0);
    lv_obj_set_style_border_width(settings_edit_row_, 1, 0);
    lv_obj_set_style_pad_all(settings_edit_row_, 3, 0);
    reactive::bind_theme(settings_edit_row_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(settings_edit_row_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(settings_edit_row_, LV_OBJ_FLAG_HIDDEN);

    // MAP view: a centred PPI canvas flanked by colour-coded short-name columns.
    map_view_ = lv_obj_create(content);
    lv_obj_remove_style_all(map_view_);
    lv_obj_set_size(map_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(map_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(map_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(map_view_, 0, 0);
    lv_obj_add_flag(map_view_, LV_OBJ_FLAG_HIDDEN);

    // Square PPI radar canvas.
    map_buf_.assign(static_cast<size_t>(kMapSize) * kMapSize, 0u);
    map_canvas_ = lv_canvas_create(map_view_);
    lv_canvas_set_buffer(map_canvas_, map_buf_.data(), kMapSize, kMapSize,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(map_canvas_, kMapSize, kMapSize);
    lv_canvas_fill_bg(map_canvas_, lv_color_black(), LV_OPA_COVER);
    lv_obj_align(map_canvas_, LV_ALIGN_CENTER, 0, 0);

    // Wider Mercator map canvas (shown instead of the radar in map mode). Kept as
    // a separate fixed-size canvas — resizing a single canvas at runtime crashes
    // the SDL/Mesa flush. Hidden by default; update_map() swaps visibility.
    map_buf_merc_.assign(static_cast<size_t>(kMapMercW) * kMapMercH, 0u);
    map_canvas_merc_ = lv_canvas_create(map_view_);
    lv_canvas_set_buffer(map_canvas_merc_, map_buf_merc_.data(), kMapMercW, kMapMercH,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(map_canvas_merc_, kMapMercW, kMapMercH);
    lv_canvas_fill_bg(map_canvas_merc_, lv_color_black(), LV_OPA_COVER);
    lv_obj_align(map_canvas_merc_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(map_canvas_merc_, LV_OBJ_FLAG_HIDDEN);

    // Three range-ring scale labels (km) over the north axis.
    map_ring_labels_.reserve(3);
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* lbl = lv_label_create(map_view_);
        lv_obj_set_style_text_font(lbl, fs, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x66aa66), 0);
        lv_label_set_text(lbl, "");
        map_ring_labels_.push_back(lbl);
    }

    // Side short-name columns: a pool of reusable row labels per side, each
    // manually pinned to its column's outer edge (left col flush-left, right col
    // flush-right). Each row carries its own font + background so the selected
    // node renders inverted (dot colour fill, black text) and self renders bold.
    constexpr int kSideRows = 7; // == kPerSide in update_map()
    const int side_rh = lv_font_get_line_height(fs) + 1; // per-row vertical step
    const auto make_side = [&](lv_align_t al, int dx) {
        lv_obj_t* c = lv_obj_create(map_view_);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 56, kSideRows * side_rh);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(c, al, dx, 2);
        return c;
    };
    map_left_  = make_side(LV_ALIGN_TOP_LEFT, 2);
    map_right_ = make_side(LV_ALIGN_TOP_RIGHT, -2);

    const auto make_rows = [&](lv_obj_t* parent, std::vector<lv_obj_t*>& pool, bool right) {
        for (int i = 0; i < kSideRows; ++i) {
            lv_obj_t* l = lv_label_create(parent);
            lv_obj_set_style_text_font(l, fs, 0);
            lv_obj_set_style_pad_hor(l, 2, 0);   // breathing room for the inverted pill
            lv_obj_set_style_radius(l, 2, 0);
            // Flush to the column's outer edge; right column also right-aligns text.
            lv_obj_align(l, right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, i * side_rh);
            if (right) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
            pool.push_back(l);
        }
    };
    make_rows(map_left_, map_left_rows_, false);
    make_rows(map_right_, map_right_rows_, true);

    // Range / status hint, bottom-centre over the scope.
    map_status_ = lv_label_create(map_view_);
    lv_obj_set_style_text_font(map_status_, fs, 0);
    lv_obj_set_style_text_color(map_status_, lv_color_hex(0x888888), 0);
    lv_label_set_text(map_status_, "");
    lv_obj_align(map_status_, LV_ALIGN_BOTTOM_MID, 0, -1);

    // TOOLS view: list on the LEFT (~110px wide), output panel on the RIGHT
    // (remaining ~210px). Side-by-side uses the landscape format efficiently,
    // mirrors the ADS-B radar/sidebar pattern and the rest of the Meshtastic UI.
    tools_view_ = lv_obj_create(content);
    lv_obj_remove_style_all(tools_view_);
    lv_obj_set_size(tools_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(tools_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tools_view_, 0, 0);
    lv_obj_add_flag(tools_view_, LV_OBJ_FLAG_HIDDEN);

    // Left column: 4-item list (≈110px wide).
    constexpr int32_t kListW = 110;
    tools_list_ = lv_label_create(tools_view_);
    lv_label_set_recolor(tools_list_, true);
    lv_label_set_long_mode(tools_list_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(tools_list_, fs, 0);
    lv_obj_set_style_pad_all(tools_list_, 3, 0);
    reactive::bind_theme(tools_list_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_set_size(tools_list_, kListW, LV_PCT(100));
    lv_obj_align(tools_list_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(tools_list_, "");

    // Thin vertical divider between list and output.
    lv_obj_t* div = lv_obj_create(tools_view_);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, 1, LV_PCT(100));
    lv_obj_set_style_bg_color(div, view::palette(vm_.is_dark_mode()).border, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_align(div, LV_ALIGN_TOP_LEFT, kListW, 0);

    // Right column: output panel (rest of width). Hidden until a tool is run.
    tools_output_ = lv_label_create(tools_view_);
    lv_label_set_recolor(tools_output_, true);
    lv_label_set_long_mode(tools_output_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(tools_output_, fs, 0);
    lv_obj_set_style_pad_all(tools_output_, 4, 0);
    lv_obj_set_size(tools_output_, 320 - kListW - 1, LV_PCT(100));
    lv_obj_align(tools_output_, LV_ALIGN_TOP_LEFT, kListW + 1, 0);
    reactive::bind_theme(tools_output_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_label_set_text(tools_output_, "");
    lv_obj_add_flag(tools_output_, LV_OBJ_FLAG_HIDDEN);

    // The "write" key bumps compose_req; observe it to enter compose mode.
    last_compose_req_ = lv_subject_get_int(vm_.compose_req_subject());
    lv_subject_add_observer(vm_.compose_req_subject(), compose_req_cb, this);

    // The "canned" key bumps canned_req; observe it to open the picker overlay.
    last_canned_req_ = lv_subject_get_int(vm_.canned_req_subject());
    lv_subject_add_observer(vm_.canned_req_subject(), canned_req_cb, this);

    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);
    tick(); // initial paint
}

void MeshtasticScreen::compose_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MeshtasticScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int v = lv_subject_get_int(subject);
    if (v == self->last_compose_req_) return; // ignore the initial notification
    self->last_compose_req_ = v;
    // Only meaningful on the CHATS page; the "write" key only maps there anyway.
    if (self->vm_.page() == static_cast<int>(MeshtasticViewModel::Page::Chats)) {
        self->enter_compose();
    }
}

void MeshtasticScreen::compose_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MeshtasticScreen*>(ctx)) self->on_compose_key(key);
}

void MeshtasticScreen::enter_compose() {
    if (compose_active_) return;
    if (canned_active_) exit_canned();
    compose_active_ = true;
    compose_buf_.clear();
    update_compose_row();
    lv_obj_remove_flag(compose_row_, LV_OBJ_FLAG_HIDDEN);
    platform::set_key_capture(compose_key_cb, this);
}

void MeshtasticScreen::exit_compose() {
    platform::set_key_capture(nullptr, nullptr);
    compose_active_ = false;
    compose_buf_.clear();
    if (compose_row_) lv_obj_add_flag(compose_row_, LV_OBJ_FLAG_HIDDEN);
}

void MeshtasticScreen::on_compose_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        if (!compose_buf_.empty()) send_current(compose_buf_);
        exit_compose(); // modal per-message: send and return to BROWSE
    } else if (key == LV_KEY_ESC) {
        exit_compose(); // cancel
    } else if (key == LV_KEY_BACKSPACE) {
        if (!compose_buf_.empty()) {
            compose_buf_.pop_back();
            update_compose_row();
        }
    } else if (key >= 0x20 && key < 0x7f) {
        if (compose_buf_.size() < 200) {
            compose_buf_.push_back(static_cast<char>(key));
            update_compose_row();
        }
    }
}

void MeshtasticScreen::update_compose_row() {
    if (!compose_row_) return;
    std::string s = "> " + compose_buf_ + "_";
    lv_label_set_text(compose_row_, s.c_str());
}

void MeshtasticScreen::canned_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MeshtasticScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int v = lv_subject_get_int(subject);
    if (v == self->last_canned_req_) return; // ignore the initial notification
    self->last_canned_req_ = v;
    if (self->vm_.page() == static_cast<int>(MeshtasticViewModel::Page::Chats)) {
        self->enter_canned();
    }
}

void MeshtasticScreen::canned_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MeshtasticScreen*>(ctx)) self->on_canned_key(key);
}

void MeshtasticScreen::enter_canned() {
    if (canned_active_) return;
    if (compose_active_) exit_compose();
    canned_active_ = true;
    std::string list = "#888888 Canned - pick 1-";
    list += std::to_string(kCannedCount);
    list += ", Esc#\n";
    for (int i = 0; i < kCannedCount; ++i) {
        char line[96];
        std::snprintf(line, sizeof(line), "#63e2b7 %d#  %s\n", i + 1, kCanned[i]);
        list += line;
    }
    lv_label_set_text(canned_box_, list.c_str());
    lv_obj_remove_flag(canned_box_, LV_OBJ_FLAG_HIDDEN);
    platform::set_key_capture(canned_key_cb, this);
}

void MeshtasticScreen::exit_canned() {
    platform::set_key_capture(nullptr, nullptr);
    canned_active_ = false;
    if (canned_box_) lv_obj_add_flag(canned_box_, LV_OBJ_FLAG_HIDDEN);
}

void MeshtasticScreen::on_canned_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        exit_canned();
    } else if (key >= '1' && key <= '9') {
        const int idx = static_cast<int>(key - '1');
        if (idx < kCannedCount) send_current(kCanned[idx]);
        exit_canned(); // modal: pick + send + return to BROWSE
    }
}

uint32_t MeshtasticScreen::send_current(const std::string& text) {
    if (!on_send_) return 0;
    if (vm_.conv_kind() == MeshtasticViewModel::Conv::Dm) {
        return on_send_(text, vm_.conv_dm_peer(), 0);
    }
    return on_send_(text, 0xFFFFFFFFu, static_cast<uint8_t>(vm_.conv_channel()));
}

std::string MeshtasticScreen::conv_title(const std::vector<toolkit::Entity>& snap) const {
    if (vm_.conv_kind() == MeshtasticViewModel::Conv::Dm) {
        const uint32_t peer = vm_.conv_dm_peer();
        for (const auto& e : snap) {
            if (e.id.size() > 1 && e.id[0] == '!') {
                const uint32_t num =
                    static_cast<uint32_t>(std::strtoul(e.id.c_str() + 1, nullptr, 16));
                if (num == peer) {
                    const std::string sh = field_of(e, "short");
                    return "@" + (sh.empty() ? e.id : sh);
                }
            }
        }
        char b[16];
        std::snprintf(b, sizeof(b), "@!%08x", peer);
        return b;
    }
    // Channel: resolve the name from the ChannelTable.
    const int idx = vm_.conv_channel();
    for (const auto& c : channels_.active()) {
        if (c.index == idx) {
            if (!c.name.empty()) return "#" + c.name;
            return c.role == 1 ? std::string("#Primary")
                               : ("#Ch" + std::to_string(idx));
        }
    }
    return "#Ch" + std::to_string(idx);
}

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

void MeshtasticScreen::update_chats(const std::vector<toolkit::Entity>& snap) {
    if (!chats_label_) return;

    // Map node number -> short name (id is "!aabbccdd") to label senders.
    std::unordered_map<uint32_t, std::string> names;
    for (const auto& e : snap) {
        if (e.id.size() > 1 && e.id[0] == '!') {
            const uint32_t num =
                static_cast<uint32_t>(std::strtoul(e.id.c_str() + 1, nullptr, 16));
            const std::string sh = field_of(e, "short");
            names[num] = sh.empty() ? e.id : sh;
        }
    }

    // Filter the feed to the current conversation (channel slot or DM peer).
    constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
    const bool dm = (vm_.conv_kind() == MeshtasticViewModel::Conv::Dm);
    const uint32_t peer = vm_.conv_dm_peer();
    const auto chan = static_cast<uint8_t>(vm_.conv_channel());
    const auto in_conv = [&](const MeshMessage& m) {
        if (dm) {
            return (m.is_self && m.to == peer) ||
                   (!m.is_self && m.from == peer && m.to != kBroadcast);
        }
        return m.channel == chan;
    };

    const auto msgs = messages_.snapshot();
    std::vector<const MeshMessage*> shown;
    for (const auto& m : msgs) if (in_conv(m)) shown.push_back(&m);
    const size_t start = shown.size() > 9 ? shown.size() - 9 : 0;

    std::string text;
    for (size_t i = start; i < shown.size(); ++i) {
        const MeshMessage& m = *shown[i];
        std::string sh;
        const auto it = names.find(m.from);
        if (it != names.end()) {
            sh = it->second;
        } else {
            char b[16];
            std::snprintf(b, sizeof(b), "!%08x", m.from);
            sh = b;
        }
        // Sender name recoloured (self accent-green, peers info-blue); body default.
        char hdr[48];
        std::snprintf(hdr, sizeof(hdr), "#%06x %s:# ",
                      m.is_self ? 0x63e2b7u : 0x70c0e8u, sh.c_str());
        text += hdr;
        text += m.text;
        // Delivery dot for our own messages: green ack / amber pending / red fail.
        if (m.is_self && m.ack != AckState::None) {
            const uint32_t c = m.ack == AckState::Delivered ? 0x63e2b7u
                               : m.ack == AckState::Failed  ? 0xe88080u
                                                            : 0xf0a020u;
            char dot[24];
            std::snprintf(dot, sizeof(dot), " #%06x \xE2\x97\x8f#", c);
            text += dot;
        }
        text += "\n";
    }
    if (shown.empty()) {
        text = "#888888 (no messages here yet)#";
    }
    lv_label_set_text(chats_label_, text.c_str());
}

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
        toolkit::map::draw_base(buf, vp, base_map_, toolkit::map::MapStyle{});
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

void MeshtasticScreen::settings_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<MeshtasticScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) return;
    const bool is_sel = (static_cast<int>(base->id1) == self->settings_sel_row_);
    const lv_draw_task_type_t type = lv_draw_task_get_type(task);
    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel && base->part == LV_PART_ITEMS) {
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(self->vm_.is_dark_mode()).primary;
        fd->opa = LV_OPA_COVER;
    } else if (type == LV_DRAW_TASK_TYPE_LABEL && is_sel) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        ld->color = lv_color_black();
    }
}

void MeshtasticScreen::update_settings(const std::vector<toolkit::Entity>& snap) {
    if (!settings_table_) return;
    // Feed the primary channel name for the Channel display row.
    for (const auto& c : channels_.active()) {
        if (c.role == 1) {
            vm_.set_settings_channel(c.name.empty() ? "#Primary" : "#" + c.name);
            break;
        }
    }
    const int n = MeshtasticViewModel::kSettingCount;
    lv_table_set_row_count(settings_table_, static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) {
        lv_table_set_cell_value(settings_table_, static_cast<uint32_t>(i), 0,
                                vm_.setting_name(i).c_str());
        lv_table_set_cell_value(settings_table_, static_cast<uint32_t>(i), 1,
                                vm_.setting_value(i).c_str());
    }
    settings_sel_row_ = vm_.settings_cursor();
    lv_obj_invalidate(settings_table_);
}

void MeshtasticScreen::settings_edit_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MeshtasticScreen*>(ctx)) self->on_settings_edit_key(key);
}

void MeshtasticScreen::on_settings_edit_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        vm_.settings_editor_commit();
        close_settings_editor();
    } else if (key == LV_KEY_ESC) {
        vm_.settings_editor_cancel();
        close_settings_editor();
    } else if (key == LV_KEY_BACKSPACE) {
        vm_.settings_editor_backspace();
        if (settings_edit_row_) {
            const std::string s = "> " + vm_.settings_editor_buf() + "_";
            lv_label_set_text(settings_edit_row_, s.c_str());
        }
    } else if (key >= 0x20 && key < 0x7f) {
        vm_.settings_editor_char(static_cast<char>(key));
        if (settings_edit_row_) {
            const std::string s = "> " + vm_.settings_editor_buf() + "_";
            lv_label_set_text(settings_edit_row_, s.c_str());
        }
    }
}

void MeshtasticScreen::open_settings_editor() {
    if (!settings_edit_row_) return;
    const std::string s = "> " + vm_.settings_editor_buf() + "_";
    lv_label_set_text(settings_edit_row_, s.c_str());
    lv_obj_remove_flag(settings_edit_row_, LV_OBJ_FLAG_HIDDEN);
    platform::set_key_capture(settings_edit_key_cb, this);
}

void MeshtasticScreen::close_settings_editor() {
    platform::set_key_capture(nullptr, nullptr);
    if (settings_edit_row_) lv_obj_add_flag(settings_edit_row_, LV_OBJ_FLAG_HIDDEN);
}

void MeshtasticScreen::update_tools(const std::vector<toolkit::Entity>& snap) {
    if (!tools_list_) return;

    const int cur = vm_.tools_cursor();
    const bool out_open = vm_.tools_output_open();
    const auto pal = view::palette(vm_.is_dark_mode());

    // Tool list: 4 items; cursor row = green band text, V2 items = dim grey.
    struct ToolItem { const char* label; bool v2; };
    static constexpr ToolItem kItems[] = {
        {"Mesh stats",     false},
        {"Packet log",     false},
        {"Traceroute...",  true},
        {"Telemetry req...", true},
    };
    std::string list;
    for (int i = 0; i < 4; ++i) {
        const bool is_cur = (i == cur);
        // Cursor row: accent green + › marker. V2 items: dim grey. Others: normal.
        const uint32_t col = kItems[i].v2 ? 0x616161u
                             : is_cur      ? 0x63e2b7u  // accent green
                                           : 0xe6e6e6u;
        char line[96];
        std::snprintf(line, sizeof(line), "#%06x %s %s#\n",
                      col, is_cur ? "\xE2\x80\xBA" : " ", kItems[i].label);
        list += line;
    }
    lv_label_set_text(tools_list_, list.c_str());

    // Output panel (visible when the selected tool is "running").
    if (!out_open) {
        lv_obj_add_flag(tools_output_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(tools_output_, LV_OBJ_FLAG_HIDDEN);

    const PacketCounts pc = source_.packet_counts();
    const bool connected  = source_.ok();
    const int  nodes_n    = static_cast<int>(snap.size());

    if (cur == 0) {
        // Mesh stats: link state, node count, pkts/s.
        const long now_s = static_cast<long>(std::time(nullptr));
        float rate = 0.0f;
        if (tools_last_tick_ > 0 && now_s > tools_last_tick_) {
            const int delta = pc.total - tools_last_total_;
            rate = static_cast<float>(delta) / static_cast<float>(now_s - tools_last_tick_);
        }
        tools_last_total_ = pc.total;
        tools_last_tick_  = now_s;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "#888888 LINK#  %s\n"
                      "#888888 NODES# %d\n"
                      "#888888 PKTS#  %.1f/s  (#888888 total# %d)\n",
                      connected ? "#63e2b7 CONN#" : "#e88080 DISC#",
                      nodes_n, static_cast<double>(rate), pc.total);
        lv_label_set_text(tools_output_, buf);
    } else if (cur == 1) {
        // Packet log: per-type counters.
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "#888888 TEXT#     %d\n"
                      "#888888 NODEINFO# %d\n"
                      "#888888 POS#      %d\n"
                      "#888888 TOTAL#    %d\n",
                      pc.text, pc.nodeinfo, pc.pos, pc.total);
        lv_label_set_text(tools_output_, buf);
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

void MeshtasticScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<MeshtasticScreen*>(lv_timer_get_user_data(timer));
    if (self) self->tick();
}

void MeshtasticScreen::tick() {
    store_.sweep(kNodeTtlSeconds);
    const auto snap = store_.snapshot();
    const int page = vm_.page();

    const bool nodes_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Nodes));
    const bool chats_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Chats));
    const bool map_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Map));
    const bool tools_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Tools));
    const bool settings_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Settings));
    const bool placeholder = !nodes_page && !chats_page && !map_page
                           && !tools_page && !settings_page;

    // Auto-close tools output when leaving the page.
    if (!tools_page && vm_.tools_output_open()) vm_.tools_close_output();
    // Settings editor: open/close on edge.
    const bool editing = vm_.settings_editing();
    if (settings_page && editing && !settings_was_editing_) open_settings_editor();
    if (!editing && settings_was_editing_) close_settings_editor();
    settings_was_editing_ = editing;

    // If the user cycled away from NODES, close the detail sub-screen.
    if (!nodes_page && vm_.nodes_detail_open()) vm_.close_node_detail();
    const bool detail_open = nodes_page && vm_.nodes_detail_open();

    char sub[64];
    if (map_page) {
        // The Map cares about how many nodes have a fix, not the total.
        size_t with_pos = 0;
        for (const auto& e : snap) if (e.has_pos) ++with_pos;
        std::snprintf(sub, sizeof(sub), "MAP - %zu with pos", with_pos);
    } else if (chats_page) {
        // Feed the active channel indices to the VM (for the key-5 switcher), and
        // show the current conversation name + node count.
        std::vector<int> idxs;
        for (const auto& c : channels_.active()) idxs.push_back(c.index);
        vm_.set_channels(idxs);
        std::snprintf(sub, sizeof(sub), "%s - %zu node%s", conv_title(snap).c_str(),
                      snap.size(), snap.size() == 1 ? "" : "s");
    } else {
        std::snprintf(sub, sizeof(sub), "%s - %zu node%s", vm_.page_name(page),
                      snap.size(), snap.size() == 1 ? "" : "s");
    }
    vm_.set_subtitle(sub);

    lv_obj_set_flag(nodes_view_,  LV_OBJ_FLAG_HIDDEN, !nodes_page || detail_open);
    lv_obj_set_flag(node_detail_, LV_OBJ_FLAG_HIDDEN, !detail_open);
    lv_obj_set_flag(chats_label_, LV_OBJ_FLAG_HIDDEN, !chats_page);
    lv_obj_set_flag(map_view_,    LV_OBJ_FLAG_HIDDEN, !map_page);
    lv_obj_set_flag(settings_view_, LV_OBJ_FLAG_HIDDEN, !settings_page);
    lv_obj_set_flag(tools_view_,    LV_OBJ_FLAG_HIDDEN, !tools_page);
    lv_obj_set_flag(view_label_,  LV_OBJ_FLAG_HIDDEN, !placeholder);
    lv_obj_set_flag(hint_label_,  LV_OBJ_FLAG_HIDDEN, !placeholder);

    if (nodes_page && detail_open) {
        update_node_detail(snap);
    } else if (nodes_page) {
        update_nodes(snap);
    } else if (chats_page) {
        update_chats(snap);
    } else if (map_page) {
        update_map(snap);
    } else if (tools_page) {
        update_tools(snap);
    } else if (settings_page) {
        update_settings(snap);
    } else {
        lv_label_set_text(view_label_, vm_.page_name(page));
    }
}

} // namespace meshtastic
