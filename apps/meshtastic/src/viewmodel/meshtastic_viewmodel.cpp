/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_viewmodel.h"

#include "ui_const.h"

namespace meshtastic {
namespace {

// Preset outer-ring distances for the MAP zoom (km), ascending.
constexpr double kMapRingsKm[] = {0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0};
constexpr int kMapRingCount = static_cast<int>(sizeof(kMapRingsKm) / sizeof(kMapRingsKm[0]));

int nearest_ring_idx(double km) {
    int best = 0;
    double best_d = 1e18;
    for (int i = 0; i < kMapRingCount; ++i) {
        const double d = kMapRingsKm[i] > km ? kMapRingsKm[i] - km : km - kMapRingsKm[i];
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

} // namespace

MeshtasticViewModel::MeshtasticViewModel() {
    set_nav_provider(this);
    set_title("MESH");
}

int MeshtasticViewModel::page() const {
    return lv_subject_get_int(
        const_cast<MeshtasticViewModel*>(this)->toolbar_page_subject());
}

const char* MeshtasticViewModel::page_name(int page) const {
    switch (static_cast<Page>(page)) {
        case Page::Chats:    return "CHATS";
        case Page::Nodes:    return "NODES";
        case Page::Map:      return "MAP";
        case Page::Tools:    return "TOOLS";
        case Page::Settings: return "SETTINGS";
    }
    return "";
}

int MeshtasticViewModel::nodes_cursor() const {
    return nodes_cursor_;
}

void MeshtasticViewModel::set_nodes_count(int n) {
    nodes_count_ = n < 0 ? 0 : n;
    if (nodes_cursor_ >= nodes_count_) nodes_cursor_ = nodes_count_ > 0 ? nodes_count_ - 1 : 0;
}

void MeshtasticViewModel::nodes_up() {
    if (nodes_cursor_ > 0) --nodes_cursor_;
}

void MeshtasticViewModel::nodes_down() {
    if (nodes_cursor_ + 1 < nodes_count_) ++nodes_cursor_;
}

lv_subject_t* MeshtasticViewModel::compose_req_subject() {
    return compose_req_.native();
}

void MeshtasticViewModel::request_compose() {
    lv_subject_t* s = compose_req_.native();
    lv_subject_set_int(s, lv_subject_get_int(s) + 1);
}

double MeshtasticViewModel::map_range_km() const {
    if (map_zoom_idx_ < 0) {
        return map_fit_km_ < kMapRingsKm[0] ? kMapRingsKm[0] : map_fit_km_;
    }
    return kMapRingsKm[map_zoom_idx_];
}

bool MeshtasticViewModel::map_auto_range() const {
    return map_zoom_idx_ < 0;
}

void MeshtasticViewModel::set_map_fit_km(double km) {
    map_fit_km_ = km > 0.0 ? km : kMapRingsKm[0];
}

void MeshtasticViewModel::map_zoom_in() {
    int i = map_zoom_idx_ < 0 ? nearest_ring_idx(map_range_km()) : map_zoom_idx_;
    if (i > 0) --i;
    map_zoom_idx_ = i;
}

void MeshtasticViewModel::map_zoom_out() {
    int i = map_zoom_idx_ < 0 ? nearest_ring_idx(map_range_km()) : map_zoom_idx_;
    if (i < kMapRingCount - 1) ++i;
    map_zoom_idx_ = i;
}

int MeshtasticViewModel::map_cursor() const {
    return map_cursor_;
}

void MeshtasticViewModel::set_map_count(int n) {
    map_count_ = n < 0 ? 0 : n;
    if (map_cursor_ >= map_count_) map_cursor_ = -1;
}

void MeshtasticViewModel::map_cycle_selection() {
    if (map_count_ <= 0) { map_cursor_ = -1; return; }
    ++map_cursor_;
    if (map_cursor_ >= map_count_) map_cursor_ = -1;
}

MeshtasticViewModel::Conv MeshtasticViewModel::conv_kind() const { return conv_kind_; }
int MeshtasticViewModel::conv_channel() const { return conv_channel_; }
uint32_t MeshtasticViewModel::conv_dm_peer() const { return conv_dm_peer_; }

void MeshtasticViewModel::set_channels(const std::vector<int>& active_indices) {
    channels_ = active_indices;
    if (conv_kind_ == Conv::Channel && !channels_.empty()) {
        bool found = false;
        for (int c : channels_) if (c == conv_channel_) { found = true; break; }
        if (!found) conv_channel_ = channels_.front();
    }
}

void MeshtasticViewModel::chat_cycle() {
    // From a DM, the first press returns to the channel view (the channel we left).
    if (conv_kind_ == Conv::Dm) { conv_kind_ = Conv::Channel; return; }
    if (channels_.empty()) return;
    size_t pos = 0;
    bool found = false;
    for (size_t i = 0; i < channels_.size(); ++i)
        if (channels_[i] == conv_channel_) { pos = i; found = true; break; }
    conv_channel_ = found ? channels_[(pos + 1) % channels_.size()] : channels_.front();
}

void MeshtasticViewModel::open_dm(uint32_t peer) {
    conv_kind_ = Conv::Dm;
    conv_dm_peer_ = peer;
    lv_subject_set_int(toolbar_page_subject(), static_cast<int>(Page::Chats));
}

lv_subject_t* MeshtasticViewModel::canned_req_subject() {
    return canned_req_.native();
}

void MeshtasticViewModel::request_canned() {
    lv_subject_t* s = canned_req_.native();
    lv_subject_set_int(s, lv_subject_get_int(s) + 1);
}

int MeshtasticViewModel::nav_page_count() const {
    return kPageCount; // Chats / Nodes / Map / Tools / Settings
}

void MeshtasticViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    out[0] = {"#", true, true}; // page number (text overridden by the NavBar)
    switch (static_cast<Page>(page)) {
        case Page::Chats:
            out[1] = {view::ICON_CARET_RIGHT, false, true};  // switch channel / DM
            out[2] = {view::ICON_INFO, false, true};         // canned messages
            out[3] = {view::ICON_BROADCAST, false, false};   // react (V2, greyed)
            out[4] = {view::ICON_KEYBOARD, false, true};     // write -> compose
            break;
        case Page::Nodes:
            out[1] = {view::ICON_CHART_LINE, false, true};  // sort
            out[2] = {view::ICON_CARET_UP, false, true};    // up
            out[3] = {view::ICON_CARET_DOWN, false, true};  // down
            out[4] = {view::ICON_CHECK, false, true};       // detail
            break;
        case Page::Map:
            out[1] = {view::ICON_MINUS, false, true};       // range -
            out[2] = {view::ICON_PLUS, false, true};        // range +
            out[3] = {view::ICON_BROADCAST, false, true};   // center on self
            out[4] = {view::ICON_CHECK, false, true};       // detail
            break;
        case Page::Tools:
            out[1] = {view::ICON_CARET_UP, false, true};    // up
            out[2] = {view::ICON_CARET_DOWN, false, true};  // down
            out[3] = {view::ICON_CHECK, false, true};       // run
            out[4] = {view::ICON_SIGN_OUT, false, true};    // back
            break;
        case Page::Settings:
            out[1] = {view::ICON_CARET_UP, false, true};    // up
            out[2] = {view::ICON_CARET_DOWN, false, true};  // down
            out[3] = {view::ICON_CHECK, false, true};       // edit value
            out[4] = {view::ICON_SIGN_OUT, false, true};    // exit
            break;
    }
}

void MeshtasticViewModel::nav_activate(int page, int slot) {
    switch (static_cast<Page>(page)) {
        case Page::Chats:
            // 5=switch channel, 6=canned picker, 7=react (V2, greyed), 8=write.
            if (slot == 1) chat_cycle();
            else if (slot == 2) request_canned();
            else if (slot == 4) request_compose();
            break;
        case Page::Nodes:
            // 5=sort (later), 6=up, 7=down, 8=detail (later).
            if (slot == 2) nodes_up();
            else if (slot == 3) nodes_down();
            break;
        case Page::Map:
            // 5=range-, 6=range+, 7=cycle selection, 8=detail (later).
            if (slot == 1) map_zoom_in();
            else if (slot == 2) map_zoom_out();
            else if (slot == 3) map_cycle_selection();
            break;
        case Page::Settings:
            if (slot == 4) request_quit(); // 8 = exit
            break;
        default:
            // Other views are still placeholders; ESC quits, key 4 cycles views.
            break;
    }
}

} // namespace meshtastic
