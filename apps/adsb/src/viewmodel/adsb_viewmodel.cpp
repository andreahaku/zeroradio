/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_viewmodel.h"

#include "persisted_state.h"
#include "ui_const.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace adsb {
namespace {

// Range-ring states: manual ladder steps (NM) plus an AUTO state at the top that
// fits the outer ring to the farthest aircraft. range_in zooms toward 10 NM;
// range_out widens up to AUTO (the default).
constexpr std::array<int, 6> kRingLadder = {5, 10, 20, 50, 100, 200};
constexpr int kManualCount     = static_cast<int>(kRingLadder.size());
constexpr int kRangeStateCount = kManualCount + 1; // + AUTO (top index)
// v3: the range ladder gained a 5 NM step (index 0).
constexpr int kSettingsVersion = 3;

// Short labels for the sort modes (List page slot 1).
constexpr std::array<const char*, AdsbViewModel::kSortCount> kSortLabels = {
    "CALL", "DST", "SPD", "ALT", "TRK"};

int nice_range(double nm) {
    static constexpr int kNice[] = {10, 20, 50, 100, 150, 200, 300, 400, 500};
    const double want = nm * 1.15; // ~15% headroom
    for (int v : kNice) {
        if (static_cast<double>(v) >= want) return v;
    }
    return 500;
}

} // namespace

AdsbViewModel::AdsbViewModel() {
    set_help_doc("docs/help/adsb.md"); // H
    set_nav_provider(this);
    set_title("ADSB");
    load_settings();
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
    const int idx = std::clamp(range_index(), 0, kRangeStateCount - 1);
    if (idx >= kManualCount) {
        return nice_range(observed_max_nm_); // AUTO: fit the traffic
    }
    return kRingLadder[static_cast<size_t>(idx)];
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

const std::string& AdsbViewModel::cursor_hex() const {
    return cursor_hex_;
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

void AdsbViewModel::cycle_sort() {
    // Normalize first: C++ `%` keeps the sign, so a stray negative subject value
    // would otherwise yield a negative sort mode.
    const int s = ((sort_mode() % kSortCount) + kSortCount) % kSortCount;
    sort_mode_subject_.set((s + 1) % kSortCount);
    bump_nav_refresh(); // the sort label changes
    save_settings();
}

void AdsbViewModel::select_prev() {
    if (visible_order_.empty()) return;
    auto it = std::find(visible_order_.begin(), visible_order_.end(), cursor_hex_);
    if (it == visible_order_.end()) {
        cursor_hex_ = visible_order_.front();
    } else if (it != visible_order_.begin()) {
        cursor_hex_ = *std::prev(it);
    }
}

void AdsbViewModel::select_next() {
    if (visible_order_.empty()) return;
    auto it = std::find(visible_order_.begin(), visible_order_.end(), cursor_hex_);
    if (it == visible_order_.end()) {
        cursor_hex_ = visible_order_.front();
        return;
    }
    auto next = std::next(it);
    if (next != visible_order_.end()) {
        cursor_hex_ = *next;
    }
}

void AdsbViewModel::toggle_select() {
    // Lock the cursor's aircraft as the selection, or unlock if it already is.
    if (!cursor_hex_.empty() && selected_hex_ == cursor_hex_) {
        selected_hex_.clear();
    } else {
        selected_hex_ = cursor_hex_;
    }
}

void AdsbViewModel::range_in() {
    const int idx = range_index();
    if (idx > 0) {
        range_index_subject_.set(idx - 1);
        bump_nav_refresh();
        save_settings();
    }
}

void AdsbViewModel::range_out() {
    const int idx = range_index();
    if (idx + 1 < kRangeStateCount) {
        range_index_subject_.set(idx + 1);
        bump_nav_refresh();
        save_settings();
    }
}

void AdsbViewModel::toggle_trails() {
    show_trails_subject_.set(!show_trails());
    save_settings();
}

bool AdsbViewModel::detail_show_others() const { return detail_show_others_; }

void AdsbViewModel::toggle_detail_others() {
    detail_show_others_ = !detail_show_others_;
    save_settings();
}

void AdsbViewModel::set_visible_order(std::vector<std::string> order) {
    visible_order_ = std::move(order);
    const auto present = [&](const std::string& h) {
        return !h.empty() && std::find(visible_order_.begin(), visible_order_.end(), h) !=
                                 visible_order_.end();
    };
    // The cursor always sits on an aircraft when any exist (it is the highlight).
    if (!present(cursor_hex_)) {
        cursor_hex_ = visible_order_.empty() ? std::string() : visible_order_.front();
    }
    // The locked selection is dropped if its aircraft is gone (deselected is valid).
    if (!present(selected_hex_)) {
        selected_hex_.clear();
    }
    // Select the first aircraft once, on the first populated frame, so the app
    // opens with something selected (the user can deselect afterwards).
    if (!selection_init_done_ && !visible_order_.empty()) {
        selected_hex_ = visible_order_.front();
        selection_init_done_ = true;
    }
}

// ---------------- Settings ----------------

bool   AdsbViewModel::units_km() const       { return units_km_; }
double AdsbViewModel::ttl_seconds() const     { return ttl_seconds_; }
void   AdsbViewModel::set_ttl_seconds(double s) { if (s > 0.0) ttl_seconds_ = s; }
int    AdsbViewModel::trail_len() const       { return trail_len_; }
bool   AdsbViewModel::show_ground() const     { return show_ground_; }
bool   AdsbViewModel::emergency_only() const  { return emergency_only_; }

int AdsbViewModel::settings_count() const { return 9; }

bool AdsbViewModel::map_mercator() const { return map_mercator_; }

void AdsbViewModel::toggle_map_mode() {
    map_mercator_ = !map_mercator_;
    save_settings();
}
int AdsbViewModel::settings_cursor() const { return settings_cursor_; }

void AdsbViewModel::settings_up() {
    if (settings_cursor_ > 0) --settings_cursor_;
}
void AdsbViewModel::settings_down() {
    if (settings_cursor_ + 1 < settings_count()) ++settings_cursor_;
}

void AdsbViewModel::settings_activate() {
    switch (settings_cursor_) {
        case 0: set_dark_mode(!is_dark_mode()); break;            // Theme
        case 1: units_km_ = !units_km_; break;                    // Units
        case 2: {                                                 // TTL
            static const double kTtl[] = {15, 30, 60, 120};
            int i = 0;
            for (int k = 0; k < 4; ++k) if (ttl_seconds_ == kTtl[k]) i = k;
            ttl_seconds_ = kTtl[(i + 1) % 4];
            break;
        }
        case 3: {                                                 // Range default
            const int next = (range_index() + 1) % kRangeStateCount; // ladder + AUTO
            range_index_subject_.set(next);
            bump_nav_refresh();
            break;
        }
        case 4: {                                                 // Trails
            static const int kTr[] = {0, 15, 30, 60};
            int i = 0;
            for (int k = 0; k < 4; ++k) if (trail_len_ == kTr[k]) i = k;
            trail_len_ = kTr[(i + 1) % 4];
            break;
        }
        case 5: show_ground_ = !show_ground_; break;              // Ground
        case 6: emergency_only_ = !emergency_only_; break;        // Emergency only
        case 7: map_mercator_ = !map_mercator_; break;            // Map view (Radar/Map)
        case 8: location_request_ = true; return; // Location: the screen opens the dialog
        default: break;
    }
    save_settings();
}

std::string AdsbViewModel::setting_name(int i) const {
    static const char* kNames[] = {"Theme", "Units", "TTL", "Range",
                                   "Trails", "Ground", "Emerg only", "Map view", "Location"};
    return (i >= 0 && i < settings_count()) ? kNames[i] : "";
}

std::string AdsbViewModel::setting_value(int i) const {
    switch (i) {
        case 0: return is_dark_mode() ? "Dark" : "Light";
        case 1: return units_km_ ? "km" : "NM";
        case 2: return std::to_string(static_cast<int>(ttl_seconds_)) + "s";
        case 3: return auto_range() ? std::string("AUTO")
                                    : (std::to_string(range_nm()) + "NM");
        case 4: return trail_len_ == 0 ? std::string("All") : std::to_string(trail_len_);
        case 5: return show_ground_ ? "Show" : "Hide";
        case 6: return emergency_only_ ? "On" : "Off";
        case 7: return map_mercator_ ? "Map" : "Radar";
        case 8: return location_label_;
        default: return "";
    }
}

void AdsbViewModel::load_settings() {
    const auto path = toolkit::config_file("zeroradio/adsb", "settings");
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    int ver = 0;
    if (!(in >> ver) || ver < 1 || ver > kSettingsVersion) return;
    int dark = 1, km = 0, ttl = 30, range = 5, trail = 60, ground = 1, emerg = 0;
    // Require the whole record: a truncated/corrupt file must not apply a
    // half-parsed mix of saved values and inline defaults.
    if (!(in >> dark >> km >> ttl >> range >> trail >> ground >> emerg)) return;
    set_dark_mode(dark != 0);
    units_km_ = (km != 0);
    // Validate against the same allowlists the Settings screen cycles through;
    // out-of-range values are ignored so a corrupt file can't disable expiry
    // (huge TTL) or bypass the trail cap (huge trail length).
    if (ttl == 15 || ttl == 30 || ttl == 60 || ttl == 120) ttl_seconds_ = ttl;
    // Older files predate the 5 NM step at the bottom of the ladder: shift up.
    if (ver < kSettingsVersion) ++range;
    if (range >= 0 && range < kRangeStateCount) range_index_subject_.set(range);
    if (trail == 0 || trail == 15 || trail == 30 || trail == 60) trail_len_ = trail;
    show_ground_ = (ground != 0);
    emergency_only_ = (emerg != 0);
    // v2 adds the view toggles that aren't on the Settings screen: trails on/off,
    // the List sort mode, and the Detail "show others" toggle. v1 files just keep
    // their defaults for these.
    if (ver >= 2) {
        int trails_on = 1, sort = static_cast<int>(Sort::Distance), others = 1;
        if (in >> trails_on >> sort >> others) {
            show_trails_subject_.set(trails_on != 0);
            if (sort >= 0 && sort < kSortCount) sort_mode_subject_.set(sort);
            detail_show_others_ = (others != 0);
        }
        // Appended later than the original v2 fields: older v2 files lack it and
        // just keep the default (radar). Read optionally so they still parse.
        int map_view = 0;
        if (in >> map_view) map_mercator_ = (map_view != 0);
    }
}

void AdsbViewModel::save_settings() const {
    const auto path = toolkit::config_file("zeroradio/adsb", "settings");
    if (!toolkit::ensure_parent_dir(path)) return;
    // Write to a sibling temp file and rename into place so a crash mid-write
    // can't leave a truncated settings file (which load_settings would reject).
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << kSettingsVersion << ' ' << (is_dark_mode() ? 1 : 0) << ' ' << (units_km_ ? 1 : 0) << ' '
            << static_cast<int>(ttl_seconds_) << ' ' << range_index() << ' ' << trail_len_
            << ' ' << (show_ground_ ? 1 : 0) << ' ' << (emergency_only_ ? 1 : 0) << ' '
            << (show_trails() ? 1 : 0) << ' ' << sort_mode() << ' '
            << (detail_show_others_ ? 1 : 0) << ' ' << (map_mercator_ ? 1 : 0) << '\n';
        if (!out) return; // don't rename a bad write over the good file
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::filesystem::remove(tmp, ec);
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
            out[2] = {view::ICON_CARET_UP, false, true};     // up (previous)
            out[3] = {view::ICON_CARET_DOWN, false, true};   // down (next)
            out[4] = {view::ICON_CHECK, false, true};        // select / deselect
            break;
        }
        case Screen::Radar:
            out[1] = {view::ICON_PLUS, false, true};         // zoom in
            out[2] = {view::ICON_MINUS, false, true};        // zoom out
            out[3] = {view::ICON_CHART_LINE, false, true};   // trails on/off
            out[4] = {view::ICON_MAP_TOGGLE, false, true}; // radar <-> map (globe)
            break;
        case Screen::Detail:
            out[1] = {view::ICON_PLUS, false, true};         // zoom in
            out[2] = {view::ICON_MINUS, false, true};        // zoom out
            out[3] = {view::ICON_CHART_LINE, false, true};   // trails on/off
            out[4] = {view::ICON_BROADCAST, false, true};    // show other traffic on/off
            break;
        case Screen::Settings:
            out[1] = {view::ICON_CARET_UP, false, true};     // previous setting
            out[2] = {view::ICON_CARET_DOWN, false, true};   // next setting
            out[3] = {view::ICON_CHECK, false, true};        // change value
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
            else if (slot == 4) toggle_map_mode();
            break;
        case Screen::Detail:
            if (slot == 1) range_in();
            else if (slot == 2) range_out();
            else if (slot == 3) toggle_trails();
            else if (slot == 4) toggle_detail_others();
            break;
        case Screen::Settings:
            if (slot == 1) settings_up();
            else if (slot == 2) settings_down();
            else if (slot == 3) settings_activate();
            else if (slot == 4) request_quit();
            break;
    }
}

bool AdsbViewModel::take_location_request() {
    const bool req = location_request_;
    location_request_ = false;
    return req;
}

void AdsbViewModel::set_location_label(std::string label) {
    location_set_ = !label.empty();
    location_label_ = label.empty() ? std::string("Not set") : std::move(label);
}

void AdsbViewModel::move_cursor(int dir) {
    if (screen() == static_cast<int>(Screen::List)) {
        if (dir < 0) select_prev();
        else select_next();
    } else if (screen() == static_cast<int>(Screen::Settings)) {
        if (dir < 0) settings_up();
        else settings_down();
    }
}

} // namespace adsb
