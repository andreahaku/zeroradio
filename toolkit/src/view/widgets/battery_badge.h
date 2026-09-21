/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

namespace view::widgets {

// Battery level for an app header: icon + percent in neutral grey, red below
// 20%, the accent colour and a charge mark while charging. Refreshes itself every 10 s; hidden where there is no battery
// (desktop). The label is a child of `parent`: the caller places it
// (lv_obj_align / flex) like any other header item.
class BatteryBadge {
public:
    explicit BatteryBadge(lv_obj_t* parent);
    ~BatteryBadge();
    BatteryBadge(const BatteryBadge&) = delete;
    BatteryBadge& operator=(const BatteryBadge&) = delete;

    lv_obj_t* obj() const { return label_; }

private:
    static void timer_cb(lv_timer_t* timer);
    static void deleted_cb(lv_event_t* event);
    void refresh();

    lv_obj_t* label_ = nullptr;
    lv_timer_t* timer_ = nullptr;
};

} // namespace view::widgets
