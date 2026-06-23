/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "meshtastic_client_source.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace meshtastic {

// Thread-safe ring of received chat messages. The client source appends from its
// reader thread; the screen snapshots on the UI thread (same cross-thread pattern
// as the toolkit EntityStore, but messages are a stream, not keyed entities).
class MessageLog {
public:
    MessageLog() = default;
    MessageLog(const MessageLog&) = delete;
    MessageLog& operator=(const MessageLog&) = delete;

    void add(const MeshMessage& m);
    // Update the delivery state of a sent message (matched by packet id).
    void update_ack(uint32_t id, AckState state);
    std::vector<MeshMessage> snapshot() const; // chronological, newest last
    std::size_t size() const;

private:
    static constexpr std::size_t kCap = 200; // keep memory bounded on-device
    mutable std::mutex mutex_;
    std::deque<MeshMessage> messages_;
};

} // namespace meshtastic
