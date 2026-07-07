/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "map_renderer.h"
#include "theme.h"
#include "meshtastic_screen_common.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace meshtastic {

using namespace common;

namespace {

constexpr uint32_t kTickPeriodMs = 300;
constexpr double kNodeTtlSeconds = 3600.0; // nodes beacon rarely; keep an hour

// NODES table columns (fit the 320px content width).
constexpr int32_t kColW[4] = {116, 64, 54, 62};
const char* const kColTitle[4] = {"SHORT", "SNR", "HOP", "AGE"};
constexpr int32_t kHeaderH = 15;

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
        assets().resolve("mapdata/world.rmap").string());

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
