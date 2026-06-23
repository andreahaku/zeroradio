/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "channel_table.h"

namespace meshtastic {

void ChannelTable::upsert(const ChannelInfo& c) {
    std::lock_guard<std::mutex> lk(mutex_);
    channels_[c.index] = c;
}

std::vector<ChannelInfo> ChannelTable::active() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<ChannelInfo> out;
    for (const auto& [idx, c] : channels_) {
        if (c.role != 0) out.push_back(c); // skip disabled
    }
    return out; // std::map iterates in ascending key order
}

} // namespace meshtastic
