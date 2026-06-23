/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace meshtastic {

// A decoded node update from a NodeInfo packet (step 3). The app maps it onto the
// toolkit EntityStore (merge-by-id). Fields are optional — Meshtastic spreads a
// node's data across packets, so only set what arrived.
struct NodeUpdate {
    std::string id;                       // "!aabbccdd" (or derived from num)
    bool is_self = false;
    bool has_long = false;  std::string long_name;
    bool has_short = false; std::string short_name;
    bool has_pos = false;   double lat = 0.0, lon = 0.0;
    bool has_snr = false;   float snr = 0.0f;
    bool has_hops = false;  int hops = 0;
    bool has_last_heard = false; uint32_t last_heard = 0; // epoch seconds
};

// A received text message (TEXT_MESSAGE_APP) decoded from a MeshPacket.
struct MeshMessage {
    uint32_t from = 0;       // sender node number
    bool is_self = false;    // sent by our own node
    uint8_t channel = 0;     // channel index
    std::string text;        // UTF-8 body
    uint32_t id = 0;         // packet id (for future ACK matching)
    uint32_t rx_time = 0;    // epoch seconds (0 if unknown)
};

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
    };

    MeshtasticClientSource(std::string host, uint16_t port, Callbacks cb);
    ~MeshtasticClientSource();

    MeshtasticClientSource(const MeshtasticClientSource&) = delete;
    MeshtasticClientSource& operator=(const MeshtasticClientSource&) = delete;

    void start();
    void stop();

    bool ok() const;         // connected AND handshake complete
    int  node_count() const; // nodes seen in the last completed config burst

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
