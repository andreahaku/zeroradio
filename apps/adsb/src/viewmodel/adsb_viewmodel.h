/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
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

    // The screen reports the current sorted aircraft order (hexes) each tick.
    void set_visible_order(std::vector<std::string> order);

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

    // Scratch buffer for the dynamic sort-mode NavBar label (List page slot 1).
    mutable std::string sort_label_;
};

} // namespace adsb
