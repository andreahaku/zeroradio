/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_viewmodel.h"

#include "persisted_state.h"
#include "ui_const.h"

#include <fstream>
#include <string>

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
    load_settings();
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

// ---- Settings ----

namespace {

static const char* kRegions[] = {
    "EU_868", "US", "ANZ", "JP", "CN", "KR", "IN", "SG", "TW", "RU", "NZ", "IL", "UA", "MY"
};
constexpr int kRegionCount = static_cast<int>(sizeof(kRegions) / sizeof(kRegions[0]));

} // namespace

int MeshtasticViewModel::settings_cursor() const { return settings_cursor_; }

void MeshtasticViewModel::settings_up() {
    if (settings_cursor_ > 0) --settings_cursor_;
}
void MeshtasticViewModel::settings_down() {
    if (settings_cursor_ + 1 < kSettingCount) ++settings_cursor_;
}

bool MeshtasticViewModel::settings_activate() {
    switch (settings_cursor_) {
        case 0: set_dark_mode(!is_dark_mode()); save_settings(); return false; // Theme
        case 1: // Long name — open text editor
        case 2: // Short name — open text editor
            settings_editor_open(settings_cursor_);
            return true;
        case 3: { // Region — cycle
            int i = 0;
            for (int k = 0; k < kRegionCount; ++k)
                if (settings_region_ == kRegions[k]) { i = k; break; }
            settings_region_ = kRegions[(i + 1) % kRegionCount];
            save_settings();
            return false;
        }
        case 4: return false; // Channel — read-only display (write via meshtasticd V2)
        default: return false;
    }
}

std::string MeshtasticViewModel::setting_name(int i) const {
    switch (i) {
        case 0: return "Theme";
        case 1: return "Long name";
        case 2: return "Short name";
        case 3: return "Region";
        case 4: return "Channel";
        default: return "";
    }
}

std::string MeshtasticViewModel::setting_value(int i) const {
    switch (i) {
        case 0: return is_dark_mode() ? "Dark" : "Light";
        case 1: return settings_long_name_.empty() ? "(not set)" : settings_long_name_;
        case 2: return settings_short_name_.empty() ? "(not set)" : settings_short_name_;
        case 3: return settings_region_;
        case 4: return settings_channel_.empty() ? "-" : settings_channel_;
        default: return "";
    }
}

bool MeshtasticViewModel::settings_editing() const { return settings_editing_; }

void MeshtasticViewModel::settings_editor_open(int item) {
    settings_editing_ = true;
    settings_edit_item_ = item;
    settings_edit_buf_ = (item == 1) ? settings_long_name_ : settings_short_name_;
}

void MeshtasticViewModel::settings_editor_char(char c) {
    if (settings_edit_buf_.size() < 64) settings_edit_buf_.push_back(c);
}

void MeshtasticViewModel::settings_editor_backspace() {
    if (!settings_edit_buf_.empty()) settings_edit_buf_.pop_back();
}

std::string MeshtasticViewModel::settings_editor_buf() const {
    return settings_edit_buf_;
}

void MeshtasticViewModel::settings_editor_commit() {
    if (settings_edit_item_ == 1) settings_long_name_  = settings_edit_buf_;
    else                           settings_short_name_ = settings_edit_buf_;
    settings_editing_ = false;
    settings_edit_item_ = -1;
    save_settings();
}

void MeshtasticViewModel::settings_editor_cancel() {
    settings_editing_ = false;
    settings_edit_item_ = -1;
}

const std::string& MeshtasticViewModel::setting_long_name()  const { return settings_long_name_; }
const std::string& MeshtasticViewModel::setting_short_name() const { return settings_short_name_; }
const std::string& MeshtasticViewModel::setting_region()     const { return settings_region_; }

void MeshtasticViewModel::set_settings_channel(const std::string& name) {
    settings_channel_ = name;
}

void MeshtasticViewModel::settings_exit() {
    lv_subject_set_int(toolbar_page_subject(), static_cast<int>(Page::Chats));
}

void MeshtasticViewModel::load_settings() {
    const auto path = toolkit::config_file("cardputer_radio/meshtastic", "settings");
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    int ver = 0;
    if (!(in >> ver) || ver != 1) return;
    int dark = 1;
    std::string lng, sht, reg;
    if (!(in >> dark)) return;
    set_dark_mode(dark != 0);
    if (in >> lng) settings_long_name_  = (lng == "-" ? "" : lng);
    if (in >> sht) settings_short_name_ = (sht == "-" ? "" : sht);
    if (in >> reg) {
        for (int k = 0; k < kRegionCount; ++k)
            if (reg == kRegions[k]) { settings_region_ = reg; break; }
    }
}

void MeshtasticViewModel::save_settings() const {
    const auto path = toolkit::config_file("cardputer_radio/meshtastic", "settings");
    if (path.empty()) return;
    if (!toolkit::ensure_parent_dir(path)) return;
    std::ofstream out(path);
    if (!out) return;
    out << "1\n"
        << (is_dark_mode() ? 1 : 0) << "\n"
        << (settings_long_name_.empty()  ? "-" : settings_long_name_)  << "\n"
        << (settings_short_name_.empty() ? "-" : settings_short_name_) << "\n"
        << settings_region_ << "\n";
}

// ---- end Settings ----

int  MeshtasticViewModel::tools_cursor() const { return tools_cursor_; }
bool MeshtasticViewModel::tools_output_open() const { return tools_output_open_; }

void MeshtasticViewModel::tools_cursor_up() {
    if (tools_cursor_ > 0) { --tools_cursor_; tools_output_open_ = false; }
}
void MeshtasticViewModel::tools_cursor_down() {
    if (tools_cursor_ + 1 < kToolCount) { ++tools_cursor_; tools_output_open_ = false; }
}
void MeshtasticViewModel::tools_run_toggle() {
    if (tools_cursor_ >= 2) return; // V2 items greyed
    tools_output_open_ = !tools_output_open_;
}
void MeshtasticViewModel::tools_close_output() { tools_output_open_ = false; }

bool MeshtasticViewModel::nodes_detail_open() const { return nodes_detail_open_; }
void MeshtasticViewModel::open_node_detail() { nodes_detail_open_ = true; }
void MeshtasticViewModel::close_node_detail() { nodes_detail_open_ = false; }
void MeshtasticViewModel::set_selected_node(uint32_t num) { selected_node_ = num; }
uint32_t MeshtasticViewModel::selected_node() const { return selected_node_; }

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
            if (nodes_detail_open_) {
                out[1] = {view::ICON_KEYBOARD, false, true};  // DM (write to node)
                out[2] = {view::ICON_PEAK, false, false};     // favorite (V2, greyed)
                out[3] = {view::ICON_BROADCAST, false, false};// traceroute (V2, greyed)
                out[4] = {view::ICON_SIGN_OUT, false, true};  // back to list
            } else {
                out[1] = {view::ICON_CHART_LINE, false, true};  // sort
                out[2] = {view::ICON_CARET_UP, false, true};    // up
                out[3] = {view::ICON_CARET_DOWN, false, true};  // down
                out[4] = {view::ICON_CHECK, false, true};       // detail
            }
            break;
        case Page::Map:
            out[1] = {view::ICON_MINUS, false, true};       // range -
            out[2] = {view::ICON_PLUS, false, true};        // range +
            out[3] = {view::ICON_BROADCAST, false, true};   // center on self
            out[4] = {view::ICON_CHECK, false, true};       // detail
            break;
        case Page::Tools:
            out[1] = {view::ICON_CARET_UP,   false, true};  // up
            out[2] = {view::ICON_CARET_DOWN,  false, true};  // down
            out[3] = {view::ICON_BROADCAST,   false, tools_cursor_ < 2}; // run (greyed for V2)
            out[4] = {view::ICON_SIGN_OUT,    false, true};  // back / close output
            break;
        case Page::Settings:
            out[1] = {view::ICON_CARET_UP,   false, true};  // ▲
            out[2] = {view::ICON_CARET_DOWN,  false, true};  // ▼
            out[3] = {view::ICON_CHECK,       false, true};  // ✓ edit/cycle
            out[4] = {view::ICON_SIGN_OUT,    false, true};  // ⎋ back to Chats
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
            if (nodes_detail_open_) {
                // Detail sub-screen: 5=DM, 6=fav (V2), 7=trace (V2), 8=back.
                if (slot == 1) { open_dm(selected_node_); close_node_detail(); }
                else if (slot == 4) close_node_detail();
            } else {
                // List: 5=sort (later), 6=up, 7=down, 8=open detail.
                if (slot == 2) nodes_up();
                else if (slot == 3) nodes_down();
                else if (slot == 4) open_node_detail();
            }
            break;
        case Page::Map:
            // 5=range-, 6=range+, 7=cycle selection, 8=detail (later).
            if (slot == 1) map_zoom_in();
            else if (slot == 2) map_zoom_out();
            else if (slot == 3) map_cycle_selection();
            break;
        case Page::Tools:
            if (slot == 1)      tools_cursor_up();
            else if (slot == 2) tools_cursor_down();
            else if (slot == 3) tools_run_toggle();
            else if (slot == 4) tools_close_output();
            break;
        case Page::Settings:
            if (slot == 1)      settings_up();
            else if (slot == 2) settings_down();
            else if (slot == 3) settings_activate(); // return value handled by screen
            else if (slot == 4) settings_exit();     // back to Chats
            break;
        default:
            // Other views are still placeholders; ESC quits, key 4 cycles views.
            break;
    }
}

} // namespace meshtastic
