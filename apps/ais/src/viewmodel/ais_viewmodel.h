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

namespace ais {

// AIS app state. A faithful port of AdsbViewModel: the same four screens (List /
// Radar / Detail / Settings) driven by the 5-key NavBar, the same range/trails/
// map-toggle/cursor/selection machinery and persisted settings. Only the data
// differ — vessels (MMSI / SOG / COG / heading / nav status) instead of aircraft.
class AisViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    enum class Screen : int { List = 0, Radar = 1, Detail = 2, Settings = 3 };
    enum class Sort : int { Mmsi = 0, Distance = 1, Sog = 2, Cog = 3 };
    static constexpr int kSortCount = 4;

    AisViewModel();

    // --- state accessors ---
    int screen() const;          // current screen == toolbar page
    int sort_mode() const;
    int range_nm() const;        // current outer ring in NM (manual ladder or auto-fit)
    int range_index() const;
    bool auto_range() const;
    bool show_trails() const;

    // Radar projection mode: false = azimuthal PPI radar, true = Mercator map.
    bool map_mercator() const;
    void toggle_map_mode();

    void set_observed_max_nm(double nm);

    // Two ids, both tracked by MMSI string (stable across re-sorts):
    //  - cursor: the list highlight, moved by up/down;
    //  - selected: the locked vessel (toggled with the select key), the Detail/
    //    Radar focus. Empty = none selected.
    const std::string& cursor_id() const;
    const std::string& selected_id() const;
    void set_selected_id(std::string id);

    lv_subject_t* sort_mode_subject();
    lv_subject_t* range_index_subject();
    lv_subject_t* show_trails_subject();

    void cycle_sort();
    void select_prev();
    void select_next();
    void toggle_select();
    void range_in();
    void range_out();
    void toggle_trails();

    bool detail_show_others() const;
    void toggle_detail_others();

    // The screen reports the current sorted vessel order each tick so prev/next
    // move by identity and a vanished selection snaps to the nearest vessel.
    void set_visible_order(std::vector<std::string> order);

    // --- Settings ---
    bool units_km() const;
    double ttl_seconds() const;
    void set_ttl_seconds(double s);
    int trail_len() const;
    int settings_count() const;
    int settings_cursor() const;
    void settings_up();
    void settings_down();
    void settings_activate();
    std::string setting_name(int i) const;
    std::string setting_value(int i) const;

    // Location row (last): activating it asks the screen to open the shared
    // location dialog; the label is what the row shows ("Valletta, MT").
    bool take_location_request();
    void set_location_label(std::string label);
    // False until a position is saved (or given by env): the screen then opens
    // the location dialog once, so a fresh install never centres on a default.
    bool location_set() const { return location_set_; }

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

    // F/X (arrows): the list cursor, or the settings cursor.
    void on_up() override { move_cursor(-1); }
    void on_down() override { move_cursor(1); }

private:
    bool        location_request_{false};
    std::string location_label_{"Not set"};
    bool        location_set_{false};
    void move_cursor(int dir);
    void load_settings();
    void save_settings() const;

    reactive::IntSubject  sort_mode_subject_{static_cast<int>(Sort::Distance)};
    reactive::IntSubject  range_index_subject_{5}; // AUTO
    reactive::BoolSubject show_trails_subject_{true};

    std::string cursor_id_;
    std::string selected_id_;
    bool selection_init_done_{false};
    std::vector<std::string> visible_order_;

    double observed_max_nm_{0.0};
    bool detail_show_others_{true};

    // Settings.
    bool   units_km_{false};
    double ttl_seconds_{60.0};
    int    trail_len_{60};
    bool   map_mercator_{false};
    int    settings_cursor_{0};

    mutable std::string sort_label_;
};

} // namespace ais
