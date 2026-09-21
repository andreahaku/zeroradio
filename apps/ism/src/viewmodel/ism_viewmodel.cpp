/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ism_viewmodel.h"

#include "persisted_state.h"
#include "ui_const.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace ism {
namespace {

constexpr std::array<const char*, IsmViewModel::kSortCount> kSortLabels = {
    "MODEL", "AGE", "RSSI"};

// TTL choices the Settings screen cycles through (seconds).
constexpr std::array<double, 4> kTtlLadder = {30, 60, 120, 300};

} // namespace

IsmViewModel::IsmViewModel() {
    set_nav_provider(this);
    set_title("ISM");
    load_settings();
}

int IsmViewModel::screen() const {
    return lv_subject_get_int(const_cast<IsmViewModel*>(this)->toolbar_page_subject());
}
int IsmViewModel::sort_mode() const {
    return lv_subject_get_int(const_cast<IsmViewModel*>(this)->sort_mode_subject_.native());
}
lv_subject_t* IsmViewModel::sort_mode_subject() { return sort_mode_subject_.native(); }

const std::string& IsmViewModel::cursor_key() const { return cursor_key_; }
const std::string& IsmViewModel::selected_key() const { return selected_key_; }
void IsmViewModel::set_selected_key(std::string key) { selected_key_ = std::move(key); }

void IsmViewModel::set_visible_order(std::vector<std::string> order) {
    visible_order_ = std::move(order);
    const auto present = [this](const std::string& key) {
        return std::find(visible_order_.begin(), visible_order_.end(), key) !=
               visible_order_.end();
    };
    // Keep the cursor on a live device: if it vanished (or was never set), snap
    // to the first row so up/down and select always have a target.
    if (cursor_key_.empty() || !present(cursor_key_))
        cursor_key_ = visible_order_.empty() ? std::string{} : visible_order_.front();
    // Drop a locked selection whose device aged out, so Detail doesn't pin a
    // ghost and the List title stops showing a stale key.
    if (!selected_key_.empty() && !present(selected_key_)) selected_key_.clear();
}

int IsmViewModel::cursor_index() const {
    const auto it = std::find(visible_order_.begin(), visible_order_.end(), cursor_key_);
    return it == visible_order_.end() ? -1 : static_cast<int>(it - visible_order_.begin());
}

void IsmViewModel::cycle_sort() {
    sort_mode_subject_.set((sort_mode() + 1) % kSortCount);
    bump_nav_refresh();
}

void IsmViewModel::select_prev() {
    if (visible_order_.empty()) return;
    const int i = cursor_index();
    const int n = static_cast<int>(visible_order_.size());
    cursor_key_ = visible_order_[(i <= 0 ? n : i) - 1];
}

void IsmViewModel::select_next() {
    if (visible_order_.empty()) return;
    const int i = cursor_index();
    const int n = static_cast<int>(visible_order_.size());
    cursor_key_ = visible_order_[(i + 1) % n];
}

void IsmViewModel::toggle_select() {
    if (!selected_key_.empty()) {
        selected_key_.clear();
        return;
    }
    if (cursor_key_.empty() && !visible_order_.empty()) cursor_key_ = visible_order_.front();
    selected_key_ = cursor_key_;
}

int IsmViewModel::settings_count() const { return 2; }
int IsmViewModel::settings_cursor() const { return settings_cursor_; }

void IsmViewModel::settings_up() {
    if (settings_cursor_ > 0) --settings_cursor_;
    bump_nav_refresh();
}
void IsmViewModel::settings_down() {
    if (settings_cursor_ + 1 < settings_count()) ++settings_cursor_;
    bump_nav_refresh();
}

void IsmViewModel::settings_activate() {
    switch (settings_cursor_) {
        case 0: set_dark_mode(!is_dark_mode()); break; // Theme
        case 1: {                                      // TTL
            int i = 0;
            for (int k = 0; k < static_cast<int>(kTtlLadder.size()); ++k)
                if (ttl_seconds_ == kTtlLadder[k]) i = k;
            ttl_seconds_ = kTtlLadder[(i + 1) % kTtlLadder.size()];
            break;
        }
        default: break;
    }
    save_settings();
}

std::string IsmViewModel::setting_name(int i) const {
    static const char* kNames[] = {"Theme", "TTL"};
    return (i >= 0 && i < settings_count()) ? kNames[i] : "";
}
std::string IsmViewModel::setting_value(int i) const {
    switch (i) {
        case 0: return is_dark_mode() ? "Dark" : "Light";
        case 1: return std::to_string(static_cast<int>(ttl_seconds_)) + "s";
        default: return "";
    }
}

int IsmViewModel::nav_page_count() const { return 3; }

void IsmViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    out[0] = {"#", true, true};
    switch (static_cast<Screen>(page)) {
        case Screen::List: {
            const int s = sort_mode();
            sort_label_ = kSortLabels[s >= 0 && s < kSortCount ? s : 0];
            out[1] = {sort_label_.c_str(), true, true};    // cycle sort (shows mode)
            out[2] = {view::ICON_CARET_UP, false, true};   // previous device
            out[3] = {view::ICON_CARET_DOWN, false, true}; // next device
            out[4] = {view::ICON_CHECK, false, true};      // select / deselect
            break;
        }
        case Screen::Detail:
            out[1] = {view::ICON_CARET_UP, false, true};   // previous device
            out[2] = {view::ICON_CARET_DOWN, false, true}; // next device
            out[3] = {"", false, false};                   // (unused)
            out[4] = {view::ICON_CHECK, false, true};      // deselect / back to list
            break;
        case Screen::Settings:
            out[1] = {view::ICON_CARET_UP, false, true};   // previous setting
            out[2] = {view::ICON_CARET_DOWN, false, true}; // next setting
            out[3] = {view::ICON_CHECK, false, true};      // change value
            out[4] = {view::ICON_SIGN_OUT, false, true};   // quit
            break;
    }
}

void IsmViewModel::nav_activate(int page, int slot) {
    switch (static_cast<Screen>(page)) {
        case Screen::List:
            if (slot == 1) cycle_sort();
            else if (slot == 2) select_prev();
            else if (slot == 3) select_next();
            else if (slot == 4) toggle_select();
            break;
        case Screen::Detail:
            if (slot == 1) select_prev();
            else if (slot == 2) select_next();
            else if (slot == 4) toggle_select();
            break;
        case Screen::Settings:
            if (slot == 1) settings_up();
            else if (slot == 2) settings_down();
            else if (slot == 3) settings_activate();
            else if (slot == 4) request_quit();
            break;
    }
}

void IsmViewModel::load_settings() {
    const auto path = toolkit::config_file("zeroradio/ism", "settings");
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    int ver = 0;
    if (!(in >> ver) || ver != 1) return;
    int dark = 1, ttl = 120, sort = static_cast<int>(Sort::Age);
    if (!(in >> dark >> ttl >> sort)) return;
    set_dark_mode(dark != 0);
    for (double t : kTtlLadder)
        if (ttl == static_cast<int>(t)) ttl_seconds_ = t;
    if (sort >= 0 && sort < kSortCount) sort_mode_subject_.set(sort);
}

void IsmViewModel::save_settings() const {
    const auto path = toolkit::config_file("zeroradio/ism", "settings");
    if (!toolkit::ensure_parent_dir(path)) return;
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << 1 << ' ' << (is_dark_mode() ? 1 : 0) << ' '
            << static_cast<int>(ttl_seconds_) << ' ' << sort_mode() << '\n';
        if (!out) return;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

void IsmViewModel::move_cursor(int dir) {
    if (screen() == static_cast<int>(Screen::List) || screen() == static_cast<int>(Screen::Detail)) {
        if (dir < 0) select_prev();
        else select_next();
    } else if (screen() == static_cast<int>(Screen::Settings)) {
        if (dir < 0) settings_up();
        else settings_down();
    }
}

} // namespace ism
