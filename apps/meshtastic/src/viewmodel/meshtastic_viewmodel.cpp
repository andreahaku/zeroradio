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

int MeshtasticViewModel::nodes_cursor() const {
    return nodes_cursor_;
}

void MeshtasticViewModel::set_nodes_count(int n) {
    nodes_count_ = n < 0 ? 0 : n;
    if (nodes_cursor_ >= nodes_count_) nodes_cursor_ = nodes_count_ > 0 ? nodes_count_ - 1 : 0;
}

void MeshtasticViewModel::nodes_up() {
    if (nodes_cursor_ > 0) --nodes_cursor_;
}

void MeshtasticViewModel::nodes_down() {
    if (nodes_cursor_ + 1 < nodes_count_) ++nodes_cursor_;
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
    switch (static_cast<Page>(page)) {
        case Page::Nodes:
            // 5=sort (later), 6=up, 7=down, 8=detail (later).
            if (slot == 2) nodes_up();
            else if (slot == 3) nodes_down();
            break;
        case Page::Settings:
            if (slot == 4) request_quit(); // 8 = exit
            break;
        default:
            // Other views are still placeholders; ESC quits, key 4 cycles views.
            break;
    }
}

} // namespace meshtastic
