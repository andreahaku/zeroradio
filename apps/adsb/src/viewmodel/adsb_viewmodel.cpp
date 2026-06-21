/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_viewmodel.h"

#include "ui_const.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace adsb {
namespace {

// Range-ring states: three manual ladder steps (NM) plus an AUTO state at the
// top that fits the outer ring to the farthest aircraft. range_in zooms toward
// 50 NM; range_out widens up to AUTO (the default).
constexpr std::array<int, 3> kRingLadder = {50, 100, 200};
constexpr int kManualCount     = 3;            // ladder entries (0..2)
constexpr int kRangeStateCount = kManualCount + 1; // + AUTO (index 3)

// Round an observed max range (NM) up to a tidy outer-ring value, with headroom.
int nice_range(double nm) {
    static constexpr int kNice[] = {25, 50, 100, 150, 200, 300, 400, 500};
    const double want = nm * 1.15; // ~15% headroom so dots aren't on the edge
    for (int v : kNice) {
        if (static_cast<double>(v) >= want) return v;
    }
    return 500;
}

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
    const int idx = range_index();
    if (idx >= kManualCount) {
        return nice_range(observed_max_nm_); // AUTO: fit the traffic
    }
    return kRingLadder[static_cast<size_t>(idx < 0 ? 0 : idx)];
}

bool AdsbViewModel::auto_range() const {
    return range_index() >= kManualCount;
}

void AdsbViewModel::set_observed_max_nm(double nm) {
    observed_max_nm_ = nm < 0.0 ? 0.0 : nm;
}

const std::string& AdsbViewModel::selected_hex() const {
    return selected_hex_;
}

void AdsbViewModel::set_selected_hex(std::string hex) {
    selected_hex_ = std::move(hex);
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
    if (idx + 1 < kRangeStateCount) { // widen up to AUTO
        range_index_subject_.set(idx + 1);
        bump_nav_refresh();
    }
}

void AdsbViewModel::select_prev() {
    if (visible_order_.empty()) return;
    auto it = std::find(visible_order_.begin(), visible_order_.end(), selected_hex_);
    if (it == visible_order_.end()) {
        selected_hex_ = visible_order_.front();
    } else if (it != visible_order_.begin()) {
        selected_hex_ = *std::prev(it);
    }
}

void AdsbViewModel::select_next() {
    if (visible_order_.empty()) return;
    auto it = std::find(visible_order_.begin(), visible_order_.end(), selected_hex_);
    if (it == visible_order_.end()) {
        selected_hex_ = visible_order_.front();
        return;
    }
    auto next = std::next(it);
    if (next != visible_order_.end()) {
        selected_hex_ = *next;
    }
}

void AdsbViewModel::open_detail() {
    view_mode_subject_.set(static_cast<int>(View::Detail));
    bump_nav_refresh();
}

void AdsbViewModel::set_visible_order(std::vector<std::string> order) {
    visible_order_ = std::move(order);
    if (visible_order_.empty()) {
        selected_hex_.clear();
        return;
    }
    // Keep the current selection if it is still on screen; otherwise snap to the
    // first (nearest, by the active sort) aircraft.
    if (selected_hex_.empty() ||
        std::find(visible_order_.begin(), visible_order_.end(), selected_hex_) ==
            visible_order_.end()) {
        selected_hex_ = visible_order_.front();
    }
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
            case 2: select_next(); break;
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
