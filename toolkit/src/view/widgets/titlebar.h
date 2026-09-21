/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "battery_badge.h"

#include "lvgl.h"

#include <memory>

namespace view::widgets {

class TitleBar : public BaseWidgets {
public:
    TitleBar(lv_obj_t* parent,
             lv_subject_t* title_subject,
             lv_subject_t* subtitle_subject,
             lv_subject_t* dark_mode_subject);

    void build() override;

private:
    lv_subject_t* title_subject_;
    lv_subject_t* subtitle_subject_;
    lv_subject_t* dark_mode_subject_;
    std::unique_ptr<BatteryBadge> battery_;
};

} // namespace view::widgets
