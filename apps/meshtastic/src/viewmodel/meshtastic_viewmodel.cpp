/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_viewmodel.h"

#include "ui_const.h"

namespace meshtastic {

MeshtasticViewModel::MeshtasticViewModel() {
    set_nav_provider(this);
    set_title("MESH");
}

int MeshtasticViewModel::page() const {
    return lv_subject_get_int(
        const_cast<MeshtasticViewModel*>(this)->toolbar_page_subject());
}

const char* MeshtasticViewModel::page_name(int page) const {
    switch (static_cast<Page>(page)) {
        case Page::Chats:    return "CHATS";
        case Page::Nodes:    return "NODES";
        case Page::Map:      return "MAP";
        case Page::Tools:    return "TOOLS";
        case Page::Settings: return "SETTINGS";
    }
    return "";
}

int MeshtasticViewModel::nav_page_count() const {
    return kPageCount; // Chats / Nodes / Map / Tools / Settings
}

void MeshtasticViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    out[0] = {"#", true, true}; // page number (text overridden by the NavBar)
    switch (static_cast<Page>(page)) {
        case Page::Chats:
            out[1] = {view::ICON_CARET_RIGHT, false, true}; // switch channel / DM
            out[2] = {view::ICON_INFO, false, true};        // canned messages
            out[3] = {view::ICON_BROADCAST, false, true};   // react (V2)
            out[4] = {view::ICON_KEYBOARD, false, true};    // write -> compose
            break;
        case Page::Nodes:
            out[1] = {view::ICON_CHART_LINE, false, true};  // sort
            out[2] = {view::ICON_CARET_UP, false, true};    // up
            out[3] = {view::ICON_CARET_DOWN, false, true};  // down
            out[4] = {view::ICON_CHECK, false, true};       // detail
            break;
        case Page::Map:
            out[1] = {view::ICON_MINUS, false, true};       // range -
            out[2] = {view::ICON_PLUS, false, true};        // range +
            out[3] = {view::ICON_BROADCAST, false, true};   // center on self
            out[4] = {view::ICON_CHECK, false, true};       // detail
            break;
        case Page::Tools:
            out[1] = {view::ICON_CARET_UP, false, true};    // up
            out[2] = {view::ICON_CARET_DOWN, false, true};  // down
            out[3] = {view::ICON_CHECK, false, true};       // run
            out[4] = {view::ICON_SIGN_OUT, false, true};    // back
            break;
        case Page::Settings:
            out[1] = {view::ICON_CARET_UP, false, true};    // up
            out[2] = {view::ICON_CARET_DOWN, false, true};  // down
            out[3] = {view::ICON_CHECK, false, true};       // edit value
            out[4] = {view::ICON_SIGN_OUT, false, true};    // exit
            break;
    }
}

void MeshtasticViewModel::nav_activate(int page, int slot) {
    // Scaffold: the five views are placeholders, so the per-slot actions arrive
    // with each view. For now only the Settings "exit" key (slot 4) is wired, so
    // the app is fully drivable from the keys (key 4 cycles views, ESC also quits).
    if (static_cast<Page>(page) == Page::Settings && slot == 4) {
        request_quit();
    }
}

} // namespace meshtastic
