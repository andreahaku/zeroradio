/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "shell_viewmodel.h"
#include "subjects.h"

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace meshtastic {

// Meshtastic app state. Derives from the toolkit shell and implements NavProvider.
// The five views are the five NavBar tool pages: key 4 (slot 0) cycles them, and
// keys 5..8 are dedicated to the current view. The displayed view follows the
// toolbar page directly (same model as ADS-B). Scaffold stage: the views are
// placeholders; the real Chats/Nodes/Map/Tools/Settings content lands later.
class MeshtasticViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    enum class Page : int { Chats = 0, Nodes = 1, Map = 2, Tools = 3, Settings = 4 };
    static constexpr int kPageCount = 5;

    MeshtasticViewModel();

    // Current view == toolbar page.
    int page() const;
    const char* page_name(int page) const;

    // NODES list cursor (highlighted row). The screen reports the row count each
    // tick so up/down can clamp; the index is stable while the node set doesn't
    // change (sorting is a later step).
    int  nodes_cursor() const;
    void set_nodes_count(int n);
    void nodes_up();
    void nodes_down();

    // SETTINGS: a 2-column list (name | value), same interaction as ADS-B settings.
    // V1 items: Theme (dark/light), Long name (text edit), Short name (text edit),
    // Region (picker), Channel (read-only display from ChannelTable).
    static constexpr int kSettingCount = 6;
    int         settings_cursor() const;
    void        settings_up();
    void        settings_down();
    // Cycle a picker setting (Theme, Region) or open the text editor (Long, Short).
    // Returns true if it opened a text editor (screen must start key-capture).
    bool        settings_activate();
    std::string setting_name(int i) const;
    std::string setting_value(int i) const;
    // Text-editor callbacks (Long name, Short name).
    bool        settings_editing() const;
    void        settings_editor_open(int item);
    void        settings_editor_char(char c);
    void        settings_editor_backspace();
    std::string settings_editor_buf() const;
    void        settings_editor_commit();
    void        settings_editor_cancel();
    // Accessors for the actual setting values (read by screen/compose).
    const std::string& setting_long_name()  const;
    const std::string& setting_short_name() const;
    const std::string& setting_region()     const;
    // Update channel display name each tick (from ChannelTable snapshot).
    void set_settings_channel(const std::string& name);
    void settings_exit(); // key 8: go to Chats
    void load_settings();
    void save_settings() const;

    // TOOLS view: a list of runnable items; key 7 opens/closes the output panel for
    // V1 items; V2 items are greyed. Page-cycle auto-closes the output.
    static constexpr int kToolCount = 4; // Mesh stats, Packet log, Traceroute (V2), Telemetry (V2)
    int  tools_cursor() const;
    bool tools_output_open() const;
    void tools_cursor_up();
    void tools_cursor_down();
    void tools_run_toggle();   // key 7: open/close output; no-op for greyed items
    void tools_close_output(); // called when page cycles away

    // NODE DETAIL: a sub-screen of NODES (key 8 opens, key 8 closes; key 4 cycles
    // away and closes it). Not in the page cycle. The screen reports the selected
    // node's number each tick so DM (key 5) can target it.
    bool     nodes_detail_open() const;
    void     open_node_detail();
    void     close_node_detail();
    void     set_selected_node(uint32_t num);
    uint32_t selected_node() const;

    // CHATS compose: the "write" key (slot 4) bumps this subject; the screen
    // observes it to enter compose mode (raw key capture).
    lv_subject_t* compose_req_subject();
    void request_compose();

    // MAP (PPI radar). Centred on the self node (or the mesh centroid when self
    // has no fix). The range auto-fits all positioned nodes by default; keys 5/6
    // switch to a manual zoom step. Key 7 cycles the selection (highlighted node);
    // wrapping past the last node clears it back to the self-centred view.
    double map_range_km() const;      // current outer-ring distance (km)
    bool   map_auto_range() const;    // true while auto-fitting to all nodes
    void   set_map_fit_km(double km); // screen feeds the fitted distance each tick
    void   map_zoom_in();             // smaller ring (key 5)
    void   map_zoom_out();            // larger ring (key 6)

    int  map_cursor() const;          // -1 = none (self-centred), else positioned idx
    void set_map_count(int n);        // screen reports the positioned-node count
    void map_cycle_selection();       // key 7: -1 -> 0 -> ... -> n-1 -> -1

    // MAP projection mode: false = azimuthal PPI radar (range/bearing rings),
    // true = conformal Mercator map with the coastline/border base layer. Toggled
    // by key 8 in the Map view or the "Map view" settings row; persisted.
    bool map_mercator() const;
    void toggle_map_mode();

    // CHATS conversation: the feed/compose target is either a channel slot or a DM
    // peer. Key 5 cycles the active channels; the DM entry point is set by Node
    // detail (open_dm), which also switches the view back to Chats.
    enum class Conv : int { Channel = 0, Dm = 1 };
    Conv     conv_kind() const;
    int      conv_channel() const;    // current channel index
    uint32_t conv_dm_peer() const;    // current DM peer node-num
    void     set_channels(const std::vector<int>& active_indices); // sorted, from screen
    void     chat_cycle();            // key 5: next active channel
    void     open_dm(uint32_t peer);  // Node-detail entry: DM + switch to Chats

    // CHATS canned-message picker: key 6 bumps this subject; the screen opens the
    // overlay (raw key capture, like compose).
    lv_subject_t* canned_req_subject();
    void request_canned();

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

private:
    int nodes_cursor_ = 0;
    int nodes_count_ = 0;
    bool nodes_detail_open_ = false;
    uint32_t selected_node_ = 0;
    reactive::IntSubject compose_req_{0};

    int    map_zoom_idx_ = -1;  // -1 == auto-fit; else index into the ring table
    double map_fit_km_   = 5.0; // last fitted-to-all-nodes distance (from the screen)
    int    map_cursor_   = -1;  // selected positioned node, -1 = none
    int    map_count_    = 0;   // positioned-node count (from the screen)
    bool   map_mercator_ = false; // false = radar PPI, true = Mercator map view

    int  settings_cursor_   = 0;
    bool settings_editing_  = false;
    int  settings_edit_item_= -1;
    std::string settings_edit_buf_;
    std::string settings_long_name_;
    std::string settings_short_name_;
    std::string settings_region_   = "EU_868";
    std::string settings_channel_;

    int  tools_cursor_      = 0;
    bool tools_output_open_ = false;

    Conv             conv_kind_    = Conv::Channel;
    int              conv_channel_ = 0;
    uint32_t         conv_dm_peer_ = 0;
    std::vector<int> channels_;          // active channel indices (from the screen)
    reactive::IntSubject canned_req_{0};
};

} // namespace meshtastic
