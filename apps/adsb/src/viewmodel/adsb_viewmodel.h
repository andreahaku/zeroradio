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
    int range_nm() const;        // current outer ring in NM (from the ladder)
    int range_index() const;
    int selected_index() const;

    lv_subject_t* view_mode_subject();
    lv_subject_t* sort_mode_subject();
    lv_subject_t* range_index_subject();
    lv_subject_t* selected_index_subject();

    // --- actions (wired to NavBar slots / keys) ---
    void cycle_view();
    void cycle_sort();
    void range_in();             // smaller outer ring
    void range_out();            // larger outer ring
    void select_prev();
    void select_next(int count); // clamp against the visible row count
    void set_selected(int index);
    void open_detail();

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

private:
    reactive::IntSubject view_mode_subject_{static_cast<int>(View::List)};
    reactive::IntSubject sort_mode_subject_{static_cast<int>(Sort::Range)};
    reactive::IntSubject range_index_subject_{1}; // index into the ring ladder
    reactive::IntSubject selected_index_subject_{0};
};

} // namespace adsb
