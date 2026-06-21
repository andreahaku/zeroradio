/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_viewmodel.h"

#include "ui_const.h"

#include <array>

namespace adsb {
namespace {

// Range-ring ladder (NM): the outer ring cycles through these.
constexpr std::array<int, 3> kRingLadder = {50, 100, 200};

// Two tool pages drive the 5 keys (slot 0 / key 4 always cycles the page, as in
// SDRTerminal — maximum flexibility). Slots 1..4 map to keys 5..8:
//   Page 0 (view):  1 cycle view  2 cycle sort   3 range-      4 range+
//   Page 1 (list):  1 select prev 2 select next  3 open detail 4 (reserved)
enum class Page : int { View = 0, List = 1 };
constexpr int kPageCount = 2;

} // namespace

AdsbViewModel::AdsbViewModel() {
    set_nav_provider(this);
    set_title("ADSB");
}

int AdsbViewModel::view_mode() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(view_mode_subject_.native()));
}

int AdsbViewModel::sort_mode() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(sort_mode_subject_.native()));
}

int AdsbViewModel::range_index() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(range_index_subject_.native()));
}

int AdsbViewModel::range_nm() const {
    int idx = range_index();
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(kRingLadder.size())) idx = static_cast<int>(kRingLadder.size()) - 1;
    return kRingLadder[static_cast<size_t>(idx)];
}

int AdsbViewModel::selected_index() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(selected_index_subject_.native()));
}

lv_subject_t* AdsbViewModel::view_mode_subject() {
    return view_mode_subject_.native();
}

lv_subject_t* AdsbViewModel::sort_mode_subject() {
    return sort_mode_subject_.native();
}

lv_subject_t* AdsbViewModel::range_index_subject() {
    return range_index_subject_.native();
}

lv_subject_t* AdsbViewModel::selected_index_subject() {
    return selected_index_subject_.native();
}

void AdsbViewModel::cycle_view() {
    view_mode_subject_.set((view_mode() + 1) % 3);
    bump_nav_refresh();
}

void AdsbViewModel::cycle_sort() {
    sort_mode_subject_.set((sort_mode() + 1) % 3);
    bump_nav_refresh();
}

void AdsbViewModel::range_in() {
    const int idx = range_index();
    if (idx > 0) {
        range_index_subject_.set(idx - 1);
        bump_nav_refresh();
    }
}

void AdsbViewModel::range_out() {
    const int idx = range_index();
    if (idx + 1 < static_cast<int>(kRingLadder.size())) {
        range_index_subject_.set(idx + 1);
        bump_nav_refresh();
    }
}

void AdsbViewModel::select_prev() {
    const int idx = selected_index();
    if (idx > 0) {
        selected_index_subject_.set(idx - 1);
    }
}

void AdsbViewModel::select_next(int count) {
    const int idx = selected_index();
    if (idx + 1 < count) {
        selected_index_subject_.set(idx + 1);
    }
}

void AdsbViewModel::set_selected(int index) {
    if (index < 0) index = 0;
    selected_index_subject_.set(index);
}

void AdsbViewModel::open_detail() {
    view_mode_subject_.set(static_cast<int>(View::Detail));
    bump_nav_refresh();
}

void AdsbViewModel::set_visible_count(int count) {
    visible_count_ = count < 0 ? 0 : count;
}

int AdsbViewModel::nav_page_count() const {
    return kPageCount;
}

void AdsbViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    // Slot 0 is overridden by the NavBar (page number); we still fill it for
    // clarity but its text is ignored.
    out[0] = {"#", true, true};
    if (page == static_cast<int>(Page::List)) {
        out[1] = {view::ICON_CARET_LEFT, false, true};   // select previous
        out[2] = {view::ICON_CARET_RIGHT, false, true};  // select next
        out[3] = {view::ICON_INFO, false, true};         // open detail of selection
        out[4] = {"", false, false};                     // reserved (filters later)
    } else { // Page::View
        out[1] = {view::ICON_MODE, false, true};   // cycle view
        out[2] = {view::ICON_BAND, false, true};   // cycle sort
        out[3] = {view::ICON_MINUS, false, true};  // range- (smaller outer ring)
        out[4] = {view::ICON_PLUS, false, true};   // range+ (larger outer ring)
    }
}

void AdsbViewModel::nav_activate(int page, int slot) {
    if (page == static_cast<int>(Page::List)) {
        switch (slot) {
            case 1: select_prev(); break;
            case 2: select_next(visible_count_); break;
            case 3: open_detail(); break;
            default: break;
        }
        return;
    }
    // Page::View
    switch (slot) {
        case 1: cycle_view(); break;
        case 2: cycle_sort(); break;
        case 3: range_in(); break;
        case 4: range_out(); break;
        default: break;
    }
}

} // namespace adsb
