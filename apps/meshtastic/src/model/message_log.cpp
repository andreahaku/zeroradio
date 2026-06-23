/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
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

std::vector<MeshMessage> MessageLog::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<MeshMessage>(messages_.begin(), messages_.end());
}

std::size_t MessageLog::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.size();
}

} // namespace meshtastic
