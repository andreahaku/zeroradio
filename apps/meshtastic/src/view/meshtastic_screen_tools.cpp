/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "theme.h"
#include "meshtastic_screen_common.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace meshtastic {

using namespace common;

void MeshtasticScreen::update_tools(const std::vector<toolkit::Entity>& snap) {
    if (!tools_list_) return;

    const int cur = vm_.tools_cursor();
    const bool out_open = vm_.tools_output_open();
    const auto pal = view::palette(vm_.is_dark_mode());

    // Tool list: 4 items; cursor row = green band text, V2 items = dim grey.
    struct ToolItem { const char* label; bool v2; };
    static constexpr ToolItem kItems[] = {
        {"Mesh stats",     false},
        {"Packet log",     false},
        {"Traceroute...",  true},
        {"Telemetry req...", true},
    };
    std::string list;
    for (int i = 0; i < 4; ++i) {
        const bool is_cur = (i == cur);
        // Cursor row: accent green + › marker. V2 items: dim grey. Others: normal.
        const uint32_t col = kItems[i].v2 ? 0x616161u
                             : is_cur      ? 0x63e2b7u  // accent green
                                           : 0xe6e6e6u;
        char line[96];
        std::snprintf(line, sizeof(line), "#%06x %s %s#\n",
                      col, is_cur ? "\xE2\x80\xBA" : " ", kItems[i].label);
        list += line;
    }
    lv_label_set_text(tools_list_, list.c_str());

    // Output panel (visible when the selected tool is "running").
    if (!out_open) {
        lv_obj_add_flag(tools_output_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(tools_output_, LV_OBJ_FLAG_HIDDEN);

    const PacketCounts pc = source_.packet_counts();
    const bool connected  = source_.ok();
    const int  nodes_n    = static_cast<int>(snap.size());

    if (cur == 0) {
        // Mesh stats: link state, node count, pkts/s.
        const long now_s = static_cast<long>(std::time(nullptr));
        float rate = 0.0f;
        if (tools_last_tick_ > 0 && now_s > tools_last_tick_) {
            const int delta = pc.total - tools_last_total_;
            rate = static_cast<float>(delta) / static_cast<float>(now_s - tools_last_tick_);
        }
        tools_last_total_ = pc.total;
        tools_last_tick_  = now_s;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "#888888 LINK#  %s\n"
                      "#888888 NODES# %d\n"
                      "#888888 PKTS#  %.1f/s  (#888888 total# %d)\n",
                      connected ? "#63e2b7 CONN#" : "#e88080 DISC#",
                      nodes_n, static_cast<double>(rate), pc.total);
        lv_label_set_text(tools_output_, buf);
    } else if (cur == 1) {
        // Packet log: per-type counters.
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "#888888 TEXT#     %d\n"
                      "#888888 NODEINFO# %d\n"
                      "#888888 POS#      %d\n"
                      "#888888 TOTAL#    %d\n",
                      pc.text, pc.nodeinfo, pc.pos, pc.total);
        lv_label_set_text(tools_output_, buf);
    }
}

} // namespace meshtastic
