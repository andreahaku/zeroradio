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
constexpr int kManualCount     = 3;
constexpr int kRangeStateCount = kManualCount + 1; // + AUTO (index 3)

// Short labels for the sort modes (List page slot 1).
constexpr std::array<const char*, AdsbViewModel::kSortCount> kSortLabels = {
    "CALL", "DST", "SPD", "ALT", "TRK"};

int nice_range(double nm) {
    static constexpr int kNice[] = {25, 50, 100, 150, 200, 300, 400, 500};
    const double want = nm * 1.15; // ~15% headroom
    for (int v : kNice) {
        if (static_cast<double>(v) >= want) return v;
    }
    return 500;
}

} // namespace

AdsbViewModel::AdsbViewModel() {
    set_nav_provider(this);
    set_title("ADSB");
}

int AdsbViewModel::screen() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(
        const_cast<AdsbViewModel*>(this)->toolbar_page_subject()));
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

bool AdsbViewModel::show_trails() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(show_trails_subject_.native())) != 0;
}

bool AdsbViewModel::show_labels() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(show_labels_subject_.native())) != 0;
}

const std::string& AdsbViewModel::selected_hex() const {
    return selected_hex_;
}

void AdsbViewModel::set_selected_hex(std::string hex) {
    selected_hex_ = std::move(hex);
}

lv_subject_t* AdsbViewModel::sort_mode_subject()   { return sort_mode_subject_.native(); }
lv_subject_t* AdsbViewModel::range_index_subject() { return range_index_subject_.native(); }
lv_subject_t* AdsbViewModel::show_trails_subject() { return show_trails_subject_.native(); }
lv_subject_t* AdsbViewModel::show_labels_subject() { return show_labels_subject_.native(); }

void AdsbViewModel::cycle_sort() {
    sort_mode_subject_.set((sort_mode() + 1) % kSortCount);
    bump_nav_refresh(); // the sort label changes
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

void AdsbViewModel::toggle_select() {
    if (!selected_hex_.empty()) {
        selected_hex_.clear(); // deselect
    } else if (!visible_order_.empty()) {
        selected_hex_ = visible_order_.front();
    }
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
    if (idx + 1 < kRangeStateCount) {
        range_index_subject_.set(idx + 1);
        bump_nav_refresh();
    }
}

void AdsbViewModel::toggle_trails() {
    show_trails_subject_.set(!show_trails());
}

void AdsbViewModel::toggle_labels() {
    show_labels_subject_.set(!show_labels());
}

void AdsbViewModel::set_visible_order(std::vector<std::string> order) {
    visible_order_ = std::move(order);
    // Drop the selection if its aircraft is gone; never auto-select (deselected
    // is a valid state).
    if (!selected_hex_.empty() &&
        std::find(visible_order_.begin(), visible_order_.end(), selected_hex_) ==
            visible_order_.end()) {
        selected_hex_.clear();
    }
}

int AdsbViewModel::nav_page_count() const {
    return 4; // List / Radar / Detail / Settings
}

void AdsbViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    out[0] = {"#", true, true}; // page number (text overridden by the NavBar)
    switch (static_cast<Screen>(page)) {
        case Screen::List: {
            const int s = sort_mode();
            sort_label_ = kSortLabels[s >= 0 && s < kSortCount ? s : 0];
            out[1] = {sort_label_.c_str(), true, true};      // cycle sort (shows mode)
            out[2] = {view::ICON_CARET_LEFT, false, true};   // up (previous)
            out[3] = {view::ICON_CARET_RIGHT, false, true};  // down (next)
            out[4] = {view::ICON_INFO, false, true};         // select / deselect
            break;
        }
        case Screen::Radar:
            out[1] = {view::ICON_PLUS, false, true};         // zoom in
            out[2] = {view::ICON_MINUS, false, true};        // zoom out
            out[3] = {view::ICON_GRID_TIME, false, true};    // trails on/off
            out[4] = {view::ICON_GRID_FREQ, false, true};    // labels on/off
            break;
        case Screen::Detail:
            out[1] = {view::ICON_GRID_TIME, false, true};    // trails on/off
            out[2] = {view::ICON_GRID_FREQ, false, true};    // labels on/off
            out[3] = {"", false, false};                     // reserved
            out[4] = {"", false, false};                     // reserved
            break;
        case Screen::Settings:
            out[1] = {"", false, false};
            out[2] = {"", false, false};
            out[3] = {"", false, false};
            out[4] = {view::ICON_SIGN_OUT, false, true};     // quit
            break;
    }
}

void AdsbViewModel::nav_activate(int page, int slot) {
    switch (static_cast<Screen>(page)) {
        case Screen::List:
            if (slot == 1) cycle_sort();
            else if (slot == 2) select_prev();
            else if (slot == 3) select_next();
            else if (slot == 4) toggle_select();
            break;
        case Screen::Radar:
            if (slot == 1) range_in();
            else if (slot == 2) range_out();
            else if (slot == 3) toggle_trails();
            else if (slot == 4) toggle_labels();
            break;
        case Screen::Detail:
            if (slot == 1) toggle_trails();
            else if (slot == 2) toggle_labels();
            break;
        case Screen::Settings:
            if (slot == 4) request_quit();
            break;
    }
}

} // namespace adsb
