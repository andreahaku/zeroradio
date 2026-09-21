/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * LVGL configuration, based on LVGL's lv_conf_template.h (MIT, LVGL Kft).
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#if USE_DESKTOP
#include "lv_conf_desktop.h"
#else
#include "lv_conf_cm0.h"
#endif

#endif // LV_CONF_H
