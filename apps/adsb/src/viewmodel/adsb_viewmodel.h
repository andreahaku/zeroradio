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

#include <string>
#include <vector>

namespace adsb {

// ADS-B app state. Derives from the toolkit shell and implements NavProvider.
// The four screens are the four NavBar tool pages: key 4 (slot 0) cycles them,
// and the other four keys are dedicated to the current screen. The displayed
// view follows the toolbar page directly.
class AdsbViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    enum class Screen : int { List = 0, Radar = 1, Detail = 2, Settings = 3 };
    enum class Sort : int { Callsign = 0, Distance = 1, Speed = 2, Alt = 3, Track = 4 };
    static constexpr int kSortCount = 5;

    AdsbViewModel();

    // --- state accessors ---
    int screen() const;          // current screen == toolbar page
    int sort_mode() const;
    int range_nm() const;        // current outer ring in NM (manual ladder or auto-fit)
    int range_index() const;
    bool auto_range() const;
    bool show_trails() const;    // draw position trails on the radar

    // Radar projection mode: false = azimuthal PPI radar (range/bearing rings),
    // true = conformal Mercator map with the coastline/border base layer. Toggled
    // by key 8 on the Radar screen or the "Map view" settings row; persisted.
    bool map_mercator() const;
    void toggle_map_mode();

    // The screen reports the farthest in-range aircraft each tick (auto range).
    void set_observed_max_nm(double nm);

    // Two ids, both tracked by hex (stable across re-sorts):
    //  - cursor: the list highlight, moved by up/down (always on an aircraft when
    //    any exist);
    //  - selected: the locked aircraft (toggled with the select key), which is the
    //    Detail/Radar focus and gets a marker in the list. Empty = none selected.
    const std::string& cursor_hex() const;
    const std::string& selected_hex() const;
    void set_selected_hex(std::string hex);

    lv_subject_t* sort_mode_subject();
    lv_subject_t* range_index_subject();
    lv_subject_t* show_trails_subject();

    // --- actions (wired to NavBar slots per page) ---
    void cycle_sort();           // callsign / distance / speed / alt / track
    void select_prev();          // previous aircraft in the current sorted order
    void select_next();          // next aircraft in the current sorted order
    void toggle_select();        // select the first / deselect the current aircraft
    void range_in();             // zoom in  (smaller outer ring)
    void range_out();            // zoom out (larger outer ring, up to AUTO)
    void toggle_trails();
    // Detail mini-radar: show every aircraft (like the Radar screen) or only the
    // selected one. Toggled from the Detail page; default on.
    bool detail_show_others() const;
    void toggle_detail_others();

    // The screen reports the current sorted aircraft order (hexes) each tick.
    void set_visible_order(std::vector<std::string> order);

    // --- Settings screen (navigable list: up/down move the cursor, "enter"
    // cycles the focused value). Persisted across runs. ---
    int  settings_count() const;
    int  settings_cursor() const;
    void settings_up();
    void settings_down();
    void settings_activate();      // cycle the focused setting's value
    std::string setting_name(int i) const;
    std::string setting_value(int i) const;

    // Location row (last): activating it asks the screen to open the shared
    // location dialog; the label is what the row shows ("Valletta, MT").
    bool take_location_request();
    void set_location_label(std::string label);

    // Applied settings, read by the screen.
    bool   units_km() const;
    double ttl_seconds() const;
    // Override the TTL at runtime (e.g. the ADSB_TTL env var), bypassing the
    // persisted value without rewriting the settings file. No-op if s <= 0.
    void   set_ttl_seconds(double s);
    int    trail_len() const;      // 0 = trails off
    bool   show_ground() const;
    bool   emergency_only() const;

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

private:
    reactive::IntSubject  sort_mode_subject_{static_cast<int>(Sort::Distance)};
    reactive::IntSubject  range_index_subject_{5}; // 0..4 manual ladder, 5 = AUTO
    reactive::BoolSubject show_trails_subject_{true};
    double observed_max_nm_{0.0};
    std::string cursor_hex_;                  // list highlight (up/down)
    std::string selected_hex_;                // locked selection ("" = none)
    bool selection_init_done_{false};         // default-select the first aircraft once
    std::vector<std::string> visible_order_;  // sorted hexes reported by the screen

    // Settings (persisted).
    int    settings_cursor_{0};
    bool   units_km_{false};
    double ttl_seconds_{30.0};
    int    trail_len_{60}; // points (~seconds); 0 = All (unlimited)
    bool   show_ground_{true};
    bool   emergency_only_{false};
    bool   detail_show_others_{true}; // Detail radar: show all traffic vs selected only
    bool   map_mercator_{false};      // Radar screen: false = PPI radar, true = Mercator map
    bool        location_request_{false};
    std::string location_label_{"Not set"};
    void load_settings();
    void save_settings() const;

    // Scratch buffer for the dynamic sort-mode NavBar label (List page slot 1).
    mutable std::string sort_label_;
};

} // namespace adsb
