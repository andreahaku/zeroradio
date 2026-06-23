/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "message_log.h"

namespace meshtastic {

void MessageLog::add(const MeshMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_.push_back(m);
    while (messages_.size() > kCap) messages_.pop_front();
}

void MessageLog::update_ack(uint32_t id, AckState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Most-recent first: the matching sent message is usually near the end.
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->is_self && it->id == id) {
            it->ack = state;
            return;
        }
    }
}

std::vector<MeshMessage> MessageLog::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<MeshMessage>(messages_.begin(), messages_.end());
}

std::size_t MessageLog::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.size();
}

} // namespace meshtastic
