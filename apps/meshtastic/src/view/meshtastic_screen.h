/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "entity_store.h"
#include "meshtastic_viewmodel.h"

#include "lvgl.h"

namespace meshtastic {

// Screen: a TitleBar ("MESH" + "<view> · N nodes") and a solid NavBar drive the
// five keys. A UI-thread timer snapshots the EntityStore (filled by the client
// source on its reader thread) so the NODES view shows the live node list and the
// node count tracks the mesh. The other views are still placeholders (Chats / Map
// / Tools / Settings land in later steps).
class MeshtasticScreen : public screen::BaseScreen {
public:
    MeshtasticScreen(MeshtasticViewModel& vm,
                     app::AssetManager& assets,
                     toolkit::EntityStore& store);
    ~MeshtasticScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static void tick_cb(lv_timer_t* timer);
    void tick();

    MeshtasticViewModel& vm_;
    toolkit::EntityStore& store_;

    lv_obj_t* view_label_  = nullptr; // big current-view name (non-NODES pages)
    lv_obj_t* hint_label_  = nullptr; // "press 4 to switch view"
    lv_obj_t* nodes_label_ = nullptr; // NODES list (multi-line)

    const lv_font_t* font_big_   = nullptr;
    const lv_font_t* font_small_ = nullptr;

    lv_timer_t* timer_ = nullptr;
};

} // namespace meshtastic
