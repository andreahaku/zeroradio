/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "linux_input.h"
#include "meshtastic_screen_common.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace meshtastic {

using namespace common;

namespace {

// Canned quick-reply presets (CHATS key 6). Kept short for the mesh; the picker
// sends the chosen one on the current conversation by its list number.
const char* const kCanned[] = {
    "QRV (ready)",
    "On my way",
    "Roger",
    "Standby",
    "At the meeting point",
    "Need help",
    "73",
};
constexpr int kCannedCount = static_cast<int>(sizeof(kCanned) / sizeof(kCanned[0]));

} // namespace

void MeshtasticScreen::compose_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MeshtasticScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int v = lv_subject_get_int(subject);
    if (v == self->last_compose_req_) return; // ignore the initial notification
    self->last_compose_req_ = v;
    // Only meaningful on the CHATS page; the "write" key only maps there anyway.
    if (self->vm_.page() == static_cast<int>(MeshtasticViewModel::Page::Chats)) {
        self->enter_compose();
    }
}

void MeshtasticScreen::compose_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MeshtasticScreen*>(ctx)) self->on_compose_key(key);
}

void MeshtasticScreen::enter_compose() {
    if (compose_active_) return;
    if (canned_active_) exit_canned();
    compose_active_ = true;
    compose_buf_.clear();
    update_compose_row();
    lv_obj_remove_flag(compose_row_, LV_OBJ_FLAG_HIDDEN);
    platform::set_key_capture(compose_key_cb, this);
}

void MeshtasticScreen::exit_compose() {
    platform::set_key_capture(nullptr, nullptr);
    compose_active_ = false;
    compose_buf_.clear();
    if (compose_row_) lv_obj_add_flag(compose_row_, LV_OBJ_FLAG_HIDDEN);
}

void MeshtasticScreen::on_compose_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        if (!compose_buf_.empty()) send_current(compose_buf_);
        exit_compose(); // modal per-message: send and return to BROWSE
    } else if (key == LV_KEY_ESC) {
        exit_compose(); // cancel
    } else if (key == LV_KEY_BACKSPACE) {
        if (!compose_buf_.empty()) {
            compose_buf_.pop_back();
            update_compose_row();
        }
    } else if (key >= 0x20 && key < 0x7f) {
        if (compose_buf_.size() < 200) {
            compose_buf_.push_back(static_cast<char>(key));
            update_compose_row();
        }
    }
}

void MeshtasticScreen::update_compose_row() {
    if (!compose_row_) return;
    std::string s = "> " + compose_buf_ + "_";
    lv_label_set_text(compose_row_, s.c_str());
}

void MeshtasticScreen::canned_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MeshtasticScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int v = lv_subject_get_int(subject);
    if (v == self->last_canned_req_) return; // ignore the initial notification
    self->last_canned_req_ = v;
    if (self->vm_.page() == static_cast<int>(MeshtasticViewModel::Page::Chats)) {
        self->enter_canned();
    }
}

void MeshtasticScreen::canned_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MeshtasticScreen*>(ctx)) self->on_canned_key(key);
}

void MeshtasticScreen::enter_canned() {
    if (canned_active_) return;
    if (compose_active_) exit_compose();
    canned_active_ = true;
    std::string list = "#888888 Canned - pick 1-";
    list += std::to_string(kCannedCount);
    list += ", Esc#\n";
    for (int i = 0; i < kCannedCount; ++i) {
        char line[96];
        std::snprintf(line, sizeof(line), "#63e2b7 %d#  %s\n", i + 1, kCanned[i]);
        list += line;
    }
    lv_label_set_text(canned_box_, list.c_str());
    lv_obj_remove_flag(canned_box_, LV_OBJ_FLAG_HIDDEN);
    platform::set_key_capture(canned_key_cb, this);
}

void MeshtasticScreen::exit_canned() {
    platform::set_key_capture(nullptr, nullptr);
    canned_active_ = false;
    if (canned_box_) lv_obj_add_flag(canned_box_, LV_OBJ_FLAG_HIDDEN);
}

void MeshtasticScreen::on_canned_key(uint32_t key) {
    if (key == LV_KEY_ESC) {
        exit_canned();
    } else if (key >= '1' && key <= '9') {
        const int idx = static_cast<int>(key - '1');
        if (idx < kCannedCount) send_current(kCanned[idx]);
        exit_canned(); // modal: pick + send + return to BROWSE
    }
}

uint32_t MeshtasticScreen::send_current(const std::string& text) {
    if (!on_send_) return 0;
    if (vm_.conv_kind() == MeshtasticViewModel::Conv::Dm) {
        return on_send_(text, vm_.conv_dm_peer(), 0);
    }
    return on_send_(text, 0xFFFFFFFFu, static_cast<uint8_t>(vm_.conv_channel()));
}

std::string MeshtasticScreen::conv_title(const std::vector<toolkit::Entity>& snap) const {
    if (vm_.conv_kind() == MeshtasticViewModel::Conv::Dm) {
        const uint32_t peer = vm_.conv_dm_peer();
        for (const auto& e : snap) {
            if (e.id.size() > 1 && e.id[0] == '!') {
                const uint32_t num =
                    static_cast<uint32_t>(std::strtoul(e.id.c_str() + 1, nullptr, 16));
                if (num == peer) {
                    const std::string sh = field_of(e, "short");
                    return "@" + (sh.empty() ? e.id : sh);
                }
            }
        }
        char b[16];
        std::snprintf(b, sizeof(b), "@!%08x", peer);
        return b;
    }
    // Channel: resolve the name from the ChannelTable.
    const int idx = vm_.conv_channel();
    for (const auto& c : channels_.active()) {
        if (c.index == idx) {
            if (!c.name.empty()) return "#" + c.name;
            return c.role == 1 ? std::string("#Primary")
                               : ("#Ch" + std::to_string(idx));
        }
    }
    return "#Ch" + std::to_string(idx);
}

void MeshtasticScreen::update_chats(const std::vector<toolkit::Entity>& snap) {
    if (!chats_label_) return;

    // Map node number -> short name (id is "!aabbccdd") to label senders.
    std::unordered_map<uint32_t, std::string> names;
    for (const auto& e : snap) {
        if (e.id.size() > 1 && e.id[0] == '!') {
            const uint32_t num =
                static_cast<uint32_t>(std::strtoul(e.id.c_str() + 1, nullptr, 16));
            const std::string sh = field_of(e, "short");
            names[num] = sh.empty() ? e.id : sh;
        }
    }

    // Filter the feed to the current conversation (channel slot or DM peer).
    constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
    const bool dm = (vm_.conv_kind() == MeshtasticViewModel::Conv::Dm);
    const uint32_t peer = vm_.conv_dm_peer();
    const auto chan = static_cast<uint8_t>(vm_.conv_channel());
    const auto in_conv = [&](const MeshMessage& m) {
        if (dm) {
            return (m.is_self && m.to == peer) ||
                   (!m.is_self && m.from == peer && m.to != kBroadcast);
        }
        return m.channel == chan;
    };

    const auto msgs = messages_.snapshot();
    std::vector<const MeshMessage*> shown;
    for (const auto& m : msgs) if (in_conv(m)) shown.push_back(&m);
    const size_t start = shown.size() > 9 ? shown.size() - 9 : 0;

    std::string text;
    for (size_t i = start; i < shown.size(); ++i) {
        const MeshMessage& m = *shown[i];
        std::string sh;
        const auto it = names.find(m.from);
        if (it != names.end()) {
            sh = it->second;
        } else {
            char b[16];
            std::snprintf(b, sizeof(b), "!%08x", m.from);
            sh = b;
        }
        // Sender name recoloured (self accent-green, peers info-blue); body default.
        char hdr[48];
        std::snprintf(hdr, sizeof(hdr), "#%06x %s:# ",
                      m.is_self ? 0x63e2b7u : 0x70c0e8u, sh.c_str());
        text += hdr;
        text += m.text;
        // Delivery dot for our own messages: green ack / amber pending / red fail.
        if (m.is_self && m.ack != AckState::None) {
            const uint32_t c = m.ack == AckState::Delivered ? 0x63e2b7u
                               : m.ack == AckState::Failed  ? 0xe88080u
                                                            : 0xf0a020u;
            char dot[24];
            std::snprintf(dot, sizeof(dot), " #%06x \xE2\x97\x8f#", c);
            text += dot;
        }
        text += "\n";
    }
    if (shown.empty()) {
        text = "#888888 (no messages here yet)#";
    }
    lv_label_set_text(chats_label_, text.c_str());
}

} // namespace meshtastic
