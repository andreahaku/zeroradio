/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "meshtastic_viewmodel.h"

#include "lvgl.h"

namespace meshtastic {

// Scaffold screen: a TitleBar ("MESH" + the current view name) and a solid NavBar
// drive the five keys. The body is a placeholder that names the current view; a
// light timer follows the toolbar page so cycling with key 4 updates it. The real
// Chats / Nodes / Map / Tools / Settings content replaces the placeholder later.
class MeshtasticScreen : public screen::BaseScreen {
public:
    MeshtasticScreen(MeshtasticViewModel& vm, app::AssetManager& assets);
    ~MeshtasticScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static void tick_cb(lv_timer_t* timer);
    void tick();

    MeshtasticViewModel& vm_;

    lv_obj_t* view_label_ = nullptr; // big current-view name
    lv_obj_t* hint_label_ = nullptr; // "press 4 to switch view"

    const lv_font_t* font_big_   = nullptr;
    const lv_font_t* font_small_ = nullptr;

    int last_page_ = -1;
    lv_timer_t* timer_ = nullptr;
};

} // namespace meshtastic
