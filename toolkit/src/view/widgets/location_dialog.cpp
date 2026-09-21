/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "location_dialog.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdio>
#include <unistd.h>

namespace toolkit {

namespace {

constexpr size_t kMaxCities = 4;
constexpr size_t kMaxQuery = 32;
constexpr const char* kGnssDevice = "/dev/ttyS0"; // Cap LoRa-1262-GPS on the HAT port

std::string coords_text(geo::LatLon p) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.3f, %.3f", p.lat, p.lon);
    return buf;
}

} // namespace

LocationDialog::LocationDialog(app::AssetManager& assets, bool dark_mode, OnApply on_apply)
    : on_apply_(std::move(on_apply)), dark_(dark_mode) {
    cities_ = location::CityIndex::load(assets.resolve("geodata/cities.tsv").string());
    // Start the GPS right away so it searches while the user types.
    if (::access(kGnssDevice, R_OK | W_OK) == 0) {
        gnss_ = std::make_unique<location::GnssReader>(kGnssDevice);
    }

    const auto pal = view::palette(dark_);
    auto* title_font = assets.load_font("inter-semibold.ttf", 14);
    auto* text_font = assets.load_font("inter-regular.ttf", 12);
    const lv_font_t* fallback = &lv_font_montserrat_12;

    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(root_, pal.background, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 6, 0);
    lv_obj_set_style_pad_row(root_, 2, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);

    auto* title = lv_label_create(root_);
    lv_label_set_text(title, "Location");
    lv_obj_set_style_text_font(title, title_font ? title_font : fallback, 0);
    lv_obj_set_style_text_color(title, pal.primary, 0);

    input_ = lv_label_create(root_);
    lv_obj_set_width(input_, LV_PCT(100));
    lv_obj_set_style_text_font(input_, title_font ? title_font : fallback, 0);
    lv_obj_set_style_text_color(input_, pal.text, 0);
    lv_obj_set_style_border_side(input_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(input_, 1, 0);
    lv_obj_set_style_border_color(input_, pal.border, 0);

    for (size_t i = 0; i < kMaxCities + 2; ++i) {
        auto* row = lv_label_create(root_);
        lv_obj_set_width(row, LV_PCT(100));
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(row, text_font ? text_font : fallback, 0);
        lv_obj_set_style_pad_hor(row, 3, 0);
        lv_obj_set_style_radius(row, 3, 0);
        rows_.push_back(row);
    }

    auto* hint = lv_label_create(root_);
    lv_label_set_text(hint, "City or lat,lon   Up/Down pick   Enter OK   Esc cancel");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_font(hint, text_font ? text_font : fallback, 0);
    lv_obj_set_style_text_color(hint, pal.text_disabled, 0);

    rebuild_options();
    platform::set_key_capture(key_cb, this, /*text=*/true);
    timer_ = lv_timer_create(timer_cb, 500, this);
}

LocationDialog::~LocationDialog() {
    close();
}

void LocationDialog::key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<LocationDialog*>(ctx)) self->on_key(key);
}

void LocationDialog::timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<LocationDialog*>(lv_timer_get_user_data(timer));
    if (self && self->root_) {
        self->rebuild_options(); // refresh the GPS row
    }
}

void LocationDialog::on_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        close();
        return;
    }
    if (key == LV_KEY_ENTER) {
        if (highlight_ >= 0 && highlight_ < static_cast<int>(options_.size()) &&
            options_[static_cast<size_t>(highlight_)].usable) {
            const auto place = options_[static_cast<size_t>(highlight_)].place;
            close();
            if (on_apply_) on_apply_(place);
        }
        return;
    }
    if (key == LV_KEY_UP) {
        if (highlight_ > 0) --highlight_;
        render();
        return;
    }
    if (key == LV_KEY_DOWN) {
        if (highlight_ + 1 < static_cast<int>(options_.size())) ++highlight_;
        render();
        return;
    }
    if (key == LV_KEY_BACKSPACE) {
        if (!query_.empty()) query_.pop_back();
    } else if (key >= 0x20 && key < 0x7f && query_.size() < kMaxQuery) {
        query_.push_back(static_cast<char>(key));
    } else {
        return;
    }
    highlight_ = -1; // re-pick the best row for the new text
    search();
    rebuild_options();
}

void LocationDialog::search() {
    matches_ = cities_.search(query_, kMaxCities);
}

void LocationDialog::rebuild_options() {
    options_.clear();

    Option gps;
    if (!gnss_) {
        gps.text = "GPS: no receiver";
    } else {
        const auto st = gnss_->status();
        if (st.fix) {
            gps.text = "GPS: " + coords_text(*st.fix);
            gps.usable = true;
            gps.place = {*st.fix, "GPS " + coords_text(*st.fix)};
        } else if (st.data) {
            gps.text = "GPS: searching... " + std::to_string(st.satellites) + " sats";
        } else {
            gps.text = st.port_ok ? "GPS: waiting for the receiver" : "GPS: starting";
        }
    }
    options_.push_back(gps);

    if (const auto pos = location::parse_coords(query_)) {
        options_.push_back({"Coordinates " + coords_text(*pos), true, {*pos, coords_text(*pos)}});
    }
    for (const auto& city : matches_) {
        options_.push_back({city.label, true, city});
    }

    // After typing, highlight the best match; otherwise keep the user's row.
    if (highlight_ < 0 || highlight_ >= static_cast<int>(options_.size())) {
        highlight_ = options_.size() > 1 ? 1 : 0;
    }
    render();
}

void LocationDialog::render() {
    if (!root_) return;
    const auto pal = view::palette(dark_);
    lv_label_set_text_fmt(input_, "> %s_", query_.c_str());
    for (size_t i = 0; i < rows_.size(); ++i) {
        auto* row = rows_[i];
        if (i >= options_.size()) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
        const auto& opt = options_[i];
        lv_label_set_text(row, opt.text.c_str());
        const bool hl = static_cast<int>(i) == highlight_;
        lv_obj_set_style_bg_opa(row, hl ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, pal.primary, 0);
        lv_obj_set_style_text_color(
            row, hl ? lv_color_black() : (opt.usable ? pal.text : pal.text_disabled), 0);
    }
}

void LocationDialog::close() {
    if (!root_) return;
    platform::set_key_capture(nullptr, nullptr);
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    lv_obj_delete(root_);
    root_ = nullptr;
    input_ = nullptr;
    rows_.clear();
    gnss_.reset(); // stops the reader and restores the HAT 5 V rail
}

} // namespace toolkit
