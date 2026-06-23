/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "shell_viewmodel.h"
#include "subjects.h"

#include "lvgl.h"

namespace meshtastic {

// Meshtastic app state. Derives from the toolkit shell and implements NavProvider.
// The five views are the five NavBar tool pages: key 4 (slot 0) cycles them, and
// keys 5..8 are dedicated to the current view. The displayed view follows the
// toolbar page directly (same model as ADS-B). Scaffold stage: the views are
// placeholders; the real Chats/Nodes/Map/Tools/Settings content lands later.
class MeshtasticViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    enum class Page : int { Chats = 0, Nodes = 1, Map = 2, Tools = 3, Settings = 4 };
    static constexpr int kPageCount = 5;

    MeshtasticViewModel();

    // Current view == toolbar page.
    int page() const;
    const char* page_name(int page) const;

    // NODES list cursor (highlighted row). The screen reports the row count each
    // tick so up/down can clamp; the index is stable while the node set doesn't
    // change (sorting is a later step).
    int  nodes_cursor() const;
    void set_nodes_count(int n);
    void nodes_up();
    void nodes_down();

    // CHATS compose: the "write" key (slot 4) bumps this subject; the screen
    // observes it to enter compose mode (raw key capture).
    lv_subject_t* compose_req_subject();
    void request_compose();

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

private:
    int nodes_cursor_ = 0;
    int nodes_count_ = 0;
    reactive::IntSubject compose_req_{0};
};

} // namespace meshtastic
