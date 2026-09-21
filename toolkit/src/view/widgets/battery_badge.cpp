/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "battery_badge.h"

#include "battery.h"

namespace view::widgets {

namespace {

constexpr uint32_t kRefreshMs = 10000;
constexpr int kLowPercent = 20;

const char* level_symbol(int percent) {
    if (percent >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (percent >= 65) return LV_SYMBOL_BATTERY_3;
    if (percent >= 40) return LV_SYMBOL_BATTERY_2;
    if (percent >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

} // namespace

BatteryBadge::BatteryBadge(lv_obj_t* parent) {
    label_ = lv_label_create(parent);
    // Montserrat carries the LVGL battery/charge symbols (the Inter UI font doesn't).
    lv_obj_set_style_text_font(label_, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(label_, deleted_cb, LV_EVENT_DELETE, this);
    timer_ = lv_timer_create(timer_cb, kRefreshMs, this);
    refresh();
}

BatteryBadge::~BatteryBadge() {
    if (timer_) lv_timer_delete(timer_);
    if (label_) {
        lv_obj_remove_event_cb_with_user_data(label_, deleted_cb, this);
    }
}

void BatteryBadge::timer_cb(lv_timer_t* timer) {
    if (auto* self = static_cast<BatteryBadge*>(lv_timer_get_user_data(timer))) self->refresh();
}

// The screen graph may be deleted before this object: stop touching the label.
void BatteryBadge::deleted_cb(lv_event_t* event) {
    auto* self = static_cast<BatteryBadge*>(lv_event_get_user_data(event));
    if (!self) return;
    self->label_ = nullptr;
    if (self->timer_) {
        lv_timer_delete(self->timer_);
        self->timer_ = nullptr;
    }
}

void BatteryBadge::refresh() {
    if (!label_) return;
    const auto st = platform::read_battery();
    if (!st.present) {
        lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(label_, "%s%s %d%%", st.charging ? LV_SYMBOL_CHARGE " " : "",
                          level_symbol(st.percent), st.percent);
    // Neutral grey reads on both themes; red when low, the accent while charging.
    lv_color_t color = lv_color_hex(0x9a9a9a);
    if (st.charging) {
        color = lv_color_hex(0x63e2b7);
    } else if (st.percent < kLowPercent) {
        color = lv_color_hex(0xff4040);
    }
    lv_obj_set_style_text_color(label_, color, 0);
}

} // namespace view::widgets
