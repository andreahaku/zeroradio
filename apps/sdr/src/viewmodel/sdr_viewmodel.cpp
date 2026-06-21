/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "sdr_viewmodel.h"

#include "ui_const.h"

#include "lvgl.h"

namespace sdr {

SdrViewModel::SdrViewModel() {
    set_nav_provider(this);
    set_title("SDR");
}

int SdrViewModel::nav_page_count() const {
    // Scaffold: one placeholder page. The real app exposes 5 pages
    // (tuning/zoom/visual/audio/settings) — ported in a later step.
    return 1;
}

void SdrViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    LV_UNUSED(page);
    out[0] = {"#", true, true}; // slot 0: page number (overridden by the NavBar)
    out[1] = {view::ICON_CARET_LEFT, false, true};
    out[2] = {view::ICON_BAND, false, true};
    out[3] = {view::ICON_CARET_RIGHT, false, true};
    out[4] = {"", false, false}; // reserved
}

void SdrViewModel::nav_activate(int page, int slot) {
    LV_UNUSED(page);
    LV_UNUSED(slot);
    // Scaffold: no-op until the SDR actions are ported.
}

} // namespace sdr
