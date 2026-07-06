/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// The decoded-event data types (NodeUpdate, ChannelUpdate, AckState, MeshMessage,
// PacketCounts) live with the pure decoder that produces them.
#include "meshtastic_decoder.h"

namespace meshtastic {

// TCP client of a local `meshtasticd` Client API (default 127.0.0.1:4403). A
// background thread connects, performs the want_config_id handshake, and reads
// the framed (0x94 0xC3 <len16>) protobuf `FromRadio` stream — mirroring the
// toolkit's source lifecycle (FileJsonSource) and resilient reconnect
// (RtlTcpSource); socket + nanopb state live behind a pImpl.
//
// Step 2 (radio-apps/09b): connect + handshake + frame decode, reporting the
// local node number and the NodeDB size via callbacks (fired on the reader
// thread — do NOT touch LVGL from them). FromRadio→EntityStore dispatch and
// sending are later steps.
class MeshtasticClientSource {
public:
    struct Callbacks {
        // Local node number, from the MyNodeInfo (my_info) packet.
        std::function<void(uint32_t my_node_num)> on_self;
        // End of the config burst: how many NodeInfo packets arrived.
        std::function<void(int node_count)> on_config_complete;
        // A node from a NodeInfo packet (handshake burst and live updates). Fired
        // on the reader thread; the app upserts it into the EntityStore.
        std::function<void(const NodeUpdate&)> on_node;
        // A received text message (TEXT_MESSAGE_APP). Fired on the reader thread.
        std::function<void(const MeshMessage&)> on_message;
        // ACK/failure for one of our sent messages (ROUTING_APP, matched by id).
        std::function<void(uint32_t req_id, AckState)> on_ack;
        // A channel slot from a FromRadio.channel packet. Fired on the reader thread.
        std::function<void(const ChannelUpdate&)> on_channel;
    };

    MeshtasticClientSource(std::string host, uint16_t port, Callbacks cb);
    ~MeshtasticClientSource();

    MeshtasticClientSource(const MeshtasticClientSource&) = delete;
    MeshtasticClientSource& operator=(const MeshtasticClientSource&) = delete;

    void start();
    void stop();

    bool ok() const;               // connected AND handshake complete
    int  node_count() const;       // nodes seen in the last completed config burst
    PacketCounts packet_counts() const; // snapshot of cumulative counters

    // Queue a text message for transmission (TEXT_MESSAGE_APP). Thread-safe: the
    // frame is enqueued and the reader loop writes it. Echoes the message back
    // via on_message immediately (is_self) so it shows in our own feed. Returns
    // the packet id (for future ACK matching). Default: broadcast on channel 0.
    uint32_t send_text(const std::string& text, uint32_t to = 0xFFFFFFFFu,
                       uint8_t channel = 0, bool want_ack = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace meshtastic
