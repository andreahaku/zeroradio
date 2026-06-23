/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "entity_store.h"
#include "message_log.h"
#include "meshtastic_viewmodel.h"

#include "lvgl.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace meshtastic {

// Screen: a TitleBar ("MESH" + "<view> - N nodes") and a solid NavBar drive the
// five keys. A UI-thread timer snapshots the EntityStore (filled by the client
// source on its reader thread) so the NODES view shows the live node list — a
// themed lv_table (SHORT/SNR/HOP/AGE) with the chosen theme's accent: the cursor
// row a solid green band, the self node green. Other views are placeholders.
class MeshtasticScreen : public screen::BaseScreen {
public:
    MeshtasticScreen(MeshtasticViewModel& vm,
                     app::AssetManager& assets,
                     toolkit::EntityStore& store,
                     MessageLog& messages,
                     std::function<uint32_t(const std::string&)> on_send);
    ~MeshtasticScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    static void tick_cb(lv_timer_t* timer);
    void tick();
    // Per-row colouring (self = accent; cursor row = green band + black text).
    static void nodes_draw_event_cb(lv_event_t* event);
    void update_nodes(const std::vector<toolkit::Entity>& snap);
    // CHATS feed: sender short name (resolved from the node store) + text.
    void update_chats(const std::vector<toolkit::Entity>& snap);

    // CHATS compose mode (raw key capture via platform::set_key_capture).
    static void compose_req_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void compose_key_cb(uint32_t key, void* ctx);
    void enter_compose();
    void exit_compose();
    void on_compose_key(uint32_t key);
    void update_compose_row();

    MeshtasticViewModel& vm_;
    toolkit::EntityStore& store_;
    MessageLog& messages_;
    std::function<uint32_t(const std::string&)> on_send_;

    lv_obj_t* view_label_  = nullptr; // big current-view name (placeholder pages)
    lv_obj_t* hint_label_  = nullptr; // "press 4 to switch view"
    lv_obj_t* chats_label_ = nullptr; // CHATS feed (multi-line, recolour)
    lv_obj_t* compose_row_ = nullptr; // CHATS compose input ("> text_")
    lv_obj_t* nodes_view_  = nullptr; // NODES container (column header + table)
    lv_obj_t* nodes_table_ = nullptr;

    bool compose_active_ = false;
    std::string compose_buf_;
    int last_compose_req_ = 0;

    std::vector<lv_color_t> nodes_row_colors_; // index = data row
    int nodes_sel_row_ = -1;                   // cursor row, -1 = none

    const lv_font_t* font_big_   = nullptr;
    const lv_font_t* font_small_ = nullptr;

    lv_timer_t* timer_ = nullptr;
};

} // namespace meshtastic
