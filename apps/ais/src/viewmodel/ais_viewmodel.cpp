/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_viewmodel.h"

#include "persisted_state.h"
#include "ui_const.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace ais {
namespace {

constexpr std::array<int, 5> kRingLadder = {10, 20, 50, 100, 200};
constexpr int kManualCount     = 5;
constexpr int kRangeStateCount = kManualCount + 1; // + AUTO (top index)

constexpr std::array<const char*, AisViewModel::kSortCount> kSortLabels = {
    "MMSI", "DST", "SOG", "COG"};

int nice_range(double nm) {
    static constexpr int kNice[] = {10, 20, 50, 100, 150, 200, 300, 400, 500};
    const double want = nm * 1.15;
    for (int v : kNice) {
        if (static_cast<double>(v) >= want) return v;
    }
    return 500;
}

} // namespace

AisViewModel::AisViewModel() {
    set_nav_provider(this);
    set_title("AIS");
    load_settings();
}

int AisViewModel::screen() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(
        const_cast<AisViewModel*>(this)->toolbar_page_subject()));
}

int AisViewModel::sort_mode() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(sort_mode_subject_.native()));
}

int AisViewModel::range_index() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(range_index_subject_.native()));
}

int AisViewModel::range_nm() const {
    const int idx = std::clamp(range_index(), 0, kRangeStateCount - 1);
    if (idx >= kManualCount) {
        return nice_range(observed_max_nm_);
    }
    return kRingLadder[static_cast<size_t>(idx)];
}

bool AisViewModel::auto_range() const {
    return range_index() >= kManualCount;
}

void AisViewModel::set_observed_max_nm(double nm) {
    observed_max_nm_ = nm < 0.0 ? 0.0 : nm;
}

bool AisViewModel::show_trails() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(show_trails_subject_.native())) != 0;
}

const std::string& AisViewModel::cursor_id() const { return cursor_id_; }
const std::string& AisViewModel::selected_id() const { return selected_id_; }
void AisViewModel::set_selected_id(std::string id) { selected_id_ = std::move(id); }

lv_subject_t* AisViewModel::sort_mode_subject()   { return sort_mode_subject_.native(); }
lv_subject_t* AisViewModel::range_index_subject() { return range_index_subject_.native(); }
lv_subject_t* AisViewModel::show_trails_subject() { return show_trails_subject_.native(); }

void AisViewModel::cycle_sort() {
    const int s = ((sort_mode() % kSortCount) + kSortCount) % kSortCount;
    sort_mode_subject_.set((s + 1) % kSortCount);
    bump_nav_refresh();
    save_settings();
}

void AisViewModel::select_prev() {
    if (visible_order_.empty()) return;
    auto it = std::find(visible_order_.begin(), visible_order_.end(), cursor_id_);
    if (it == visible_order_.end()) {
        cursor_id_ = visible_order_.front();
    } else if (it != visible_order_.begin()) {
        cursor_id_ = *std::prev(it);
    }
}

void AisViewModel::select_next() {
    if (visible_order_.empty()) return;
    auto it = std::find(visible_order_.begin(), visible_order_.end(), cursor_id_);
    if (it == visible_order_.end()) {
        cursor_id_ = visible_order_.front();
        return;
    }
    auto next = std::next(it);
    if (next != visible_order_.end()) {
        cursor_id_ = *next;
    }
}

void AisViewModel::toggle_select() {
    if (!cursor_id_.empty() && selected_id_ == cursor_id_) {
        selected_id_.clear();
    } else {
        selected_id_ = cursor_id_;
    }
}

void AisViewModel::range_in() {
    const int idx = range_index();
    if (idx > 0) {
        range_index_subject_.set(idx - 1);
        bump_nav_refresh();
        save_settings();
    }
}

void AisViewModel::range_out() {
    const int idx = range_index();
    if (idx + 1 < kRangeStateCount) {
        range_index_subject_.set(idx + 1);
        bump_nav_refresh();
        save_settings();
    }
}

void AisViewModel::toggle_trails() {
    show_trails_subject_.set(!show_trails());
    save_settings();
}

bool AisViewModel::detail_show_others() const { return detail_show_others_; }

void AisViewModel::toggle_detail_others() {
    detail_show_others_ = !detail_show_others_;
    save_settings();
}

void AisViewModel::set_visible_order(std::vector<std::string> order) {
    visible_order_ = std::move(order);
    const auto present = [&](const std::string& h) {
        return !h.empty() && std::find(visible_order_.begin(), visible_order_.end(), h) !=
                                 visible_order_.end();
    };
    if (!present(cursor_id_)) {
        cursor_id_ = visible_order_.empty() ? std::string() : visible_order_.front();
    }
    if (!present(selected_id_)) {
        selected_id_.clear();
    }
    if (!selection_init_done_ && !visible_order_.empty()) {
        selected_id_ = visible_order_.front();
        selection_init_done_ = true;
    }
}

// ---------------- Settings ----------------

bool   AisViewModel::units_km() const         { return units_km_; }
double AisViewModel::ttl_seconds() const       { return ttl_seconds_; }
void   AisViewModel::set_ttl_seconds(double s) { if (s > 0.0) ttl_seconds_ = s; }
int    AisViewModel::trail_len() const         { return trail_len_; }

int AisViewModel::settings_count() const { return 7; }

bool AisViewModel::map_mercator() const { return map_mercator_; }

void AisViewModel::toggle_map_mode() {
    map_mercator_ = !map_mercator_;
    save_settings();
}
int AisViewModel::settings_cursor() const { return settings_cursor_; }

void AisViewModel::settings_up() {
    if (settings_cursor_ > 0) --settings_cursor_;
}
void AisViewModel::settings_down() {
    if (settings_cursor_ + 1 < settings_count()) ++settings_cursor_;
}

void AisViewModel::settings_activate() {
    switch (settings_cursor_) {
        case 0: set_dark_mode(!is_dark_mode()); break;            // Theme
        case 1: units_km_ = !units_km_; break;                    // Units
        case 2: {                                                 // TTL
            static const double kTtl[] = {30, 60, 120, 300};
            int i = 0;
            for (int k = 0; k < 4; ++k) if (ttl_seconds_ == kTtl[k]) i = k;
            ttl_seconds_ = kTtl[(i + 1) % 4];
            break;
        }
        case 3: {                                                 // Range default
            const int next = (range_index() + 1) % 6;
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
        case 5: map_mercator_ = !map_mercator_; break;            // Map view (Radar/Map)
        case 6: location_request_ = true; return; // Location: the screen opens the dialog
        default: break;
    }
    save_settings();
}

std::string AisViewModel::setting_name(int i) const {
    static const char* kNames[] = {"Theme", "Units", "TTL", "Range", "Trails", "Map view", "Location"};
    return (i >= 0 && i < settings_count()) ? kNames[i] : "";
}

std::string AisViewModel::setting_value(int i) const {
    switch (i) {
        case 0: return is_dark_mode() ? "Dark" : "Light";
        case 1: return units_km_ ? "km" : "NM";
        case 2: return std::to_string(static_cast<int>(ttl_seconds_)) + "s";
        case 3: return auto_range() ? std::string("AUTO")
                                    : (std::to_string(range_nm()) + "NM");
        case 4: return trail_len_ == 0 ? std::string("All") : std::to_string(trail_len_);
        case 5: return map_mercator_ ? "Map" : "Radar";
        case 6: return location_label_;
        default: return "";
    }
}

void AisViewModel::load_settings() {
    const auto path = toolkit::config_file("zeroradio/ais", "settings");
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    int ver = 0;
    if (!(in >> ver) || ver != 1) return;
    int dark = 1, km = 0, ttl = 60, range = 5, trail = 60, trails_on = 1,
        sort = static_cast<int>(Sort::Distance), others = 1, map_view = 0;
    if (!(in >> dark >> km >> ttl >> range >> trail >> trails_on >> sort >> others >> map_view)) {
        return; // require the whole record
    }
    set_dark_mode(dark != 0);
    units_km_ = (km != 0);
    if (ttl == 30 || ttl == 60 || ttl == 120 || ttl == 300) ttl_seconds_ = ttl;
    if (range >= 0 && range <= 5) range_index_subject_.set(range);
    if (trail == 0 || trail == 15 || trail == 30 || trail == 60) trail_len_ = trail;
    show_trails_subject_.set(trails_on != 0);
    if (sort >= 0 && sort < kSortCount) sort_mode_subject_.set(sort);
    detail_show_others_ = (others != 0);
    map_mercator_ = (map_view != 0);
}

void AisViewModel::save_settings() const {
    const auto path = toolkit::config_file("zeroradio/ais", "settings");
    if (!toolkit::ensure_parent_dir(path)) return;
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << 1 << ' ' << (is_dark_mode() ? 1 : 0) << ' ' << (units_km_ ? 1 : 0) << ' '
            << static_cast<int>(ttl_seconds_) << ' ' << range_index() << ' ' << trail_len_
            << ' ' << (show_trails() ? 1 : 0) << ' ' << sort_mode() << ' '
            << (detail_show_others_ ? 1 : 0) << ' ' << (map_mercator_ ? 1 : 0) << '\n';
        if (!out) return;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

int AisViewModel::nav_page_count() const {
    return 4; // List / Radar / Detail / Settings
}

void AisViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    out[0] = {"#", true, true};
    switch (static_cast<Screen>(page)) {
        case Screen::List: {
            const int s = sort_mode();
            sort_label_ = kSortLabels[s >= 0 && s < kSortCount ? s : 0];
            out[1] = {sort_label_.c_str(), true, true};
            out[2] = {view::ICON_CARET_UP, false, true};
            out[3] = {view::ICON_CARET_DOWN, false, true};
            out[4] = {view::ICON_CHECK, false, true};
            break;
        }
        case Screen::Radar:
            out[1] = {view::ICON_PLUS, false, true};
            out[2] = {view::ICON_MINUS, false, true};
            out[3] = {view::ICON_CHART_LINE, false, true};
            out[4] = {view::ICON_MAP_TOGGLE, map_mercator_, true};
            break;
        case Screen::Detail:
            out[1] = {view::ICON_PLUS, false, true};
            out[2] = {view::ICON_MINUS, false, true};
            out[3] = {view::ICON_CHART_LINE, false, true};
            out[4] = {view::ICON_BROADCAST, false, true};
            break;
        case Screen::Settings:
            out[1] = {view::ICON_CARET_UP, false, true};
            out[2] = {view::ICON_CARET_DOWN, false, true};
            out[3] = {view::ICON_CHECK, false, true};
            out[4] = {view::ICON_SIGN_OUT, false, true};
            break;
    }
}

void AisViewModel::nav_activate(int page, int slot) {
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

bool AisViewModel::take_location_request() {
    const bool req = location_request_;
    location_request_ = false;
    return req;
}

void AisViewModel::set_location_label(std::string label) {
    location_label_ = label.empty() ? std::string("Not set") : std::move(label);
}

} // namespace ais
