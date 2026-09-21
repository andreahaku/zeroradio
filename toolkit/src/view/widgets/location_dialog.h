/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "location.h"
#include "lvgl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {
class AssetManager;
}

namespace toolkit {

// Full-screen modal to set the suite's shared position (ADS-B / AIS home).
// Type a city ("valletta") or coordinates ("35.9, 14.51"); the first row is the
// GPS shield (live satellite count, then the fix). Up/Down pick a row, Enter
// applies it, Esc cancels. Captures the keyboard while open; the object can be
// destroyed at any time (it releases the capture and stops the GPS).
class LocationDialog {
public:
    using OnApply = std::function<void(const location::Place&)>;

    LocationDialog(app::AssetManager& assets, bool dark_mode, OnApply on_apply);
    ~LocationDialog();
    LocationDialog(const LocationDialog&) = delete;
    LocationDialog& operator=(const LocationDialog&) = delete;

    // False once the user applied or cancelled; the owner may then drop it.
    bool is_open() const { return root_ != nullptr; }

private:
    struct Option {
        std::string text;
        bool usable = false; // Enter applies it
        location::Place place;
    };

    static void key_cb(uint32_t key, void* ctx);
    static void timer_cb(lv_timer_t* timer);
    void on_key(uint32_t key);
    void search();          // on every edit: city matches for the text
    void rebuild_options(); // GPS row + coordinates + the cached matches
    void render();
    void close();

    OnApply on_apply_;
    bool dark_;
    location::CityIndex cities_;
    std::unique_ptr<location::GnssReader> gnss_;
    std::string query_;
    std::vector<location::Place> matches_;
    std::vector<Option> options_;
    int highlight_ = 0;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* input_ = nullptr;
    std::vector<lv_obj_t*> rows_;
    lv_timer_t* timer_ = nullptr;
};

} // namespace toolkit
