/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "shell_viewmodel.h"

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

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;
};

} // namespace meshtastic
