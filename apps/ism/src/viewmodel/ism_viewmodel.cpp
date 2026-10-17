/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ism_viewmodel.h"
#include "nav_hints.h"

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
    set_help_doc("docs/help/ism.md"); // H
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
    // On Detail the locked device follows the cursor, so prev/next browse the
    // devices instead of moving a cursor Detail doesn't show.
    if (screen() == static_cast<int>(Screen::Detail) && !selected_key_.empty()) {
        selected_key_ = cursor_key_;
    }
}

void IsmViewModel::select_next() {
    if (visible_order_.empty()) return;
    const int i = cursor_index();
    const int n = static_cast<int>(visible_order_.size());
    cursor_key_ = visible_order_[(i + 1) % n];
    // On Detail the locked device follows the cursor, so prev/next browse the
    // devices instead of moving a cursor Detail doesn't show.
    if (screen() == static_cast<int>(Screen::Detail) && !selected_key_.empty()) {
        selected_key_ = cursor_key_;
    }
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

void IsmViewModel::settings_activate(int dir) {
    switch (settings_cursor_) {
        case 0: set_dark_mode(!is_dark_mode()); break; // Theme
        case 1: {                                      // TTL
            int i = 0;
            for (int k = 0; k < static_cast<int>(kTtlLadder.size()); ++k)
                if (ttl_seconds_ == kTtlLadder[k]) i = k;
            const int n = static_cast<int>(kTtlLadder.size());
            ttl_seconds_ = kTtlLadder[static_cast<size_t>((i + dir + n) % n)];
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

void IsmViewModel::move_side(int dir) {
    if (screen() == static_cast<int>(Screen::Detail)) {
        if (dir < 0) select_prev();
        else select_next();
    } else if (screen() == static_cast<int>(Screen::Settings)) {
        settings_activate(dir);
    }
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

toolkit::i18n::Text IsmViewModel::nav_hint(int page, int slot) const {
    namespace h = toolkit::hints;
    switch (static_cast<Screen>(page)) {
        case Screen::List:
            switch (slot) {
                case 1: return h::kSort;
                case 2: return h::kListUp;
                case 3: return h::kListDown;
                case 4: return {"Open the highlighted device, or clear the selection",
                                 "Apri il dispositivo evidenziato, o annulla la selezione",
                                 "Ouvre l’appareil en surbrillance, ou annule la sélection",
                                 "Öffnet das markierte Gerät oder hebt die Auswahl auf",
                                 "Abre el dispositivo resaltado, o anula la selección",
                                 "ハイライト中のデバイスを開く、または選択を解除", "打开高亮的设备，或取消选择"};
            }
            break;
        case Screen::Detail:
            switch (slot) {
                case 1: return {"Previous device", "Dispositivo precedente", "Appareil précédent",
                                 "Vorheriges Gerät", "Dispositivo anterior",
                                 "前のデバイス", "上一个设备"};
                case 2: return {"Next device", "Dispositivo successivo", "Appareil suivant",
                                 "Nächstes Gerät", "Dispositivo siguiente",
                                 "次のデバイス", "下一个设备"};
                case 4: return {"Back to the device list", "Torna alla lista dei dispositivi",
                                 "Revient à la liste des appareils", "Zurück zur Geräteliste",
                                 "Vuelve a la lista de dispositivos",
                                 "デバイス一覧に戻る", "返回设备列表"};
            }
            break;
        case Screen::Settings:
            switch (slot) {
                case 1: return h::kSettingPrev;
                case 2: return h::kSettingNext;
                case 3: return h::kSettingChange;
                case 4: return h::kBackToMenu;
            }
            break;
    }
    return {};
}

} // namespace ism
