/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
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

namespace ism {

// ISM sniffer app state. Derives from the toolkit shell and implements
// NavProvider. Three screens are the three NavBar tool pages (key 4 cycles):
// List (sortable device table), Detail (selected device), Settings.
class IsmViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    enum class Screen : int { List = 0, Detail = 1, Settings = 2 };
    enum class Sort : int { Model = 0, Age = 1, Rssi = 2 };
    static constexpr int kSortCount = 3;

    IsmViewModel();

    int screen() const; // == toolbar page
    int sort_mode() const;
    lv_subject_t* sort_mode_subject();

    // Cursor = list highlight (moved by up/down); selected = the locked device
    // shown on the Detail screen. Both keyed by the stable device_key string.
    const std::string& cursor_key() const;
    const std::string& selected_key() const;
    void set_selected_key(std::string key);

    // The screen reports the visible sorted order each tick so prev/next and the
    // cursor stay valid across re-sorts.
    void set_visible_order(std::vector<std::string> order);

    // --- actions (wired to NavBar slots per page) ---
    void cycle_sort();
    void select_prev();
    void select_next();
    void toggle_select(); // lock the cursor device (or clear the lock)

    // --- Settings screen (Theme, TTL). Persisted across runs. ---
    int settings_count() const;
    int settings_cursor() const;
    void settings_up();
    void settings_down();
    void settings_activate();
    std::string setting_name(int i) const;
    std::string setting_value(int i) const;

    double ttl_seconds() const { return ttl_seconds_; }

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

    // F/X (arrows): the list cursor, or the settings cursor.
    void on_up() override { move_cursor(-1); }
    void on_down() override { move_cursor(1); }

private:
    void move_cursor(int dir);
    void load_settings();
    void save_settings() const;
    int cursor_index() const; // index of cursor_key_ in visible_order_, or -1

    reactive::IntSubject sort_mode_subject_{static_cast<int>(Sort::Age)};

    std::string cursor_key_;
    std::string selected_key_;
    std::vector<std::string> visible_order_;

    double ttl_seconds_{120.0};
    int settings_cursor_{0};

    mutable std::string sort_label_; // backing store for the nav slot label
};

} // namespace ism
