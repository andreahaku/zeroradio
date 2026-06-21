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

// ADS-B app state. Derives from the toolkit shell (dark mode, quit, NavBar page)
// and implements NavProvider to drive the 5-key bar. View/sort/range/selection
// live in their own reactive subjects so the screen can observe them.
class AdsbViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    enum class View : int { PPI = 0, List = 1, Detail = 2 };
    enum class Sort : int { Range = 0, Alt = 1, Callsign = 2 };

    AdsbViewModel();

    // --- state accessors ---
    int view_mode() const;
    int sort_mode() const;
    int range_nm() const;        // current outer ring in NM (manual ladder or auto-fit)
    int range_index() const;
    bool auto_range() const;     // true when the outer ring auto-fits the traffic

    // The screen reports the farthest in-range aircraft each tick so the auto
    // range can fit the outer ring to the traffic.
    void set_observed_max_nm(double nm);

    // Selection is tracked by the aircraft hex (stable identity), not the row
    // index: the sorted list reshuffles every refresh as aircraft move, so an
    // index would silently jump to a different aircraft.
    const std::string& selected_hex() const;
    void set_selected_hex(std::string hex);

    lv_subject_t* view_mode_subject();
    lv_subject_t* sort_mode_subject();
    lv_subject_t* range_index_subject();

    // --- actions (wired to NavBar slots / keys) ---
    void cycle_view();
    void cycle_sort();
    void range_in();             // smaller outer ring
    void range_out();            // larger outer ring
    void select_prev();          // previous aircraft in the current sorted order
    void select_next();          // next aircraft in the current sorted order
    void open_detail();

    // The screen reports the current visible aircraft order (sorted hexes) each
    // tick, so prev/next move by identity and a vanished selection snaps to the
    // first row instead of pointing at a stale index.
    void set_visible_order(std::vector<std::string> order);

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

private:
    reactive::IntSubject view_mode_subject_{static_cast<int>(View::List)};
    reactive::IntSubject sort_mode_subject_{static_cast<int>(Sort::Range)};
    reactive::IntSubject range_index_subject_{3}; // ring state: 0..2 manual ladder, 3 = AUTO
    double observed_max_nm_{0.0};                 // farthest aircraft (for auto range)
    std::string selected_hex_;                    // selected aircraft id (UI thread only)
    std::vector<std::string> visible_order_;      // sorted hexes reported by the screen
};

} // namespace adsb
