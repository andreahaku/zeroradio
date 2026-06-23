/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mutex>
#include <map>
#include <string>
#include <vector>

namespace meshtastic {

// One channel slot (0..7). Role: 0 disabled, 1 primary, 2 secondary.
struct ChannelInfo {
    int index = 0;
    std::string name; // "" for the default primary
    int role = 0;
};

// Thread-safe table of channel slots keyed by index. The client source upserts
// from its reader thread during the config burst; the screen snapshots on the UI
// thread (same cross-thread pattern as EntityStore / MessageLog).
class ChannelTable {
public:
    ChannelTable() = default;
    ChannelTable(const ChannelTable&) = delete;
    ChannelTable& operator=(const ChannelTable&) = delete;

    void upsert(const ChannelInfo& c);
    // Active channels (role != disabled), sorted by index.
    std::vector<ChannelInfo> active() const;

private:
    mutable std::mutex mutex_;
    std::map<int, ChannelInfo> channels_;
};

} // namespace meshtastic
