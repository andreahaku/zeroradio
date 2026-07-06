/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace meshtastic {

// A decoded node update from a NodeInfo packet. The app maps it onto the toolkit
// EntityStore (merge-by-id). Fields are optional — Meshtastic spreads a node's data
// across packets, so only set what arrived.
struct NodeUpdate {
    std::string id;                       // "!aabbccdd" (or derived from num)
    bool is_self = false;
    bool has_long = false;  std::string long_name;
    bool has_short = false; std::string short_name;
    bool has_pos = false;   double lat = 0.0, lon = 0.0;
    bool has_snr = false;   float snr = 0.0f;
    bool has_hops = false;  int hops = 0;
    bool has_last_heard = false; uint32_t last_heard = 0; // epoch seconds
    bool has_hw = false;    int hw_model = 0;             // HardwareModel enum
    bool has_role = false;  int role = 0;                 // DeviceConfig.Role enum
    bool has_battery = false; int battery = 0;            // percent (0..100; >100 = plugged)
    bool has_voltage = false; float voltage = 0.0f;       // volts
};

// A channel slot decoded from a FromRadio.channel packet (config burst).
struct ChannelUpdate {
    int index = 0;          // 0..7 (0 = primary)
    std::string name;       // settings.name ("" for the default primary)
    int role = 0;           // 0 disabled, 1 primary, 2 secondary
};

// Delivery state of one of our own sent messages (color-outline in the feed).
enum class AckState : uint8_t {
    None = 0,   // not applicable (received message)
    Pending,    // sent, awaiting ACK (yellow)
    Delivered,  // acknowledged (green)
    Failed,     // routing failure / no ACK (red)
};

// A text message (TEXT_MESSAGE_APP) decoded from a MeshPacket.
struct MeshMessage {
    uint32_t from = 0;       // sender node number
    uint32_t to = 0;         // destination node number (0xFFFFFFFF = broadcast)
    bool is_self = false;    // sent by our own node
    uint8_t channel = 0;     // channel index
    std::string text;        // UTF-8 body
    uint32_t id = 0;         // packet id (ACK matching)
    uint32_t rx_time = 0;    // epoch seconds (0 if unknown)
    AckState ack = AckState::None;
};

// Cumulative packet counters. Incremented on the decode thread; the getters are
// atomic reads safe from any thread (snapshot via MeshDecoder::packet_counts()).
struct PacketCounts {
    int text     = 0; // TEXT_MESSAGE_APP frames
    int nodeinfo = 0; // NodeInfo frames
    int pos      = 0; // NodeInfo frames that carried a position fix
    int total    = 0; // every successfully decoded FromRadio frame
};

// Sink for decoded events. Same callbacks MeshtasticClientSource exposed, minus the
// networking-only ones — fired synchronously from decode_from_radio()/feed().
struct DecodeSink {
    // Local node number, from the MyNodeInfo (my_info) packet.
    std::function<void(uint32_t my_node_num)> on_self;
    // End of the config burst: how many NodeInfo packets arrived.
    std::function<void(int node_count)> on_config_complete;
    // A node from a NodeInfo packet (handshake burst and live updates).
    std::function<void(const NodeUpdate&)> on_node;
    // A received text message (TEXT_MESSAGE_APP).
    std::function<void(const MeshMessage&)> on_message;
    // ACK/failure for one of our sent messages (ROUTING_APP, matched by request_id).
    std::function<void(uint32_t req_id, AckState)> on_ack;
    // A channel slot from a FromRadio.channel packet.
    std::function<void(const ChannelUpdate&)> on_channel;
};

// Pure Meshtastic FromRadio decoder: stream framing (0x94 0xC3 <len16>) + protobuf
// union dispatch -> typed events. No sockets, threads or LVGL, so it links into a
// standalone unit test against frozen vectors. Extracted from MeshtasticClientSource
// (the socket/handshake/TX lifecycle stays there and pumps bytes through feed()).
//
// Thread model (mirrors the old Impl): feed()/decode_from_radio()/begin_config() run
// on the reader thread; my_node_num()/synced()/node_count()/packet_counts() are
// atomic reads safe from other threads.
class MeshDecoder {
public:
    // config_nonce = the want_config_id echoed back in config_complete_id; only a
    // matching id ends the handshake burst (mirrors the old nonce gate).
    MeshDecoder(DecodeSink sink, uint32_t config_nonce);
    ~MeshDecoder();

    MeshDecoder(const MeshDecoder&) = delete;
    MeshDecoder& operator=(const MeshDecoder&) = delete;

    // Decode ONE serialized FromRadio (framing already stripped). Returns true iff the
    // protobuf decoded; on success fires the relevant sink callback, bumps the total
    // counter, and updates burst/sync state. Unknown variants/portnums decode true but
    // fire nothing.
    bool decode_from_radio(const uint8_t* data, size_t len);

    // Feed a chunk of the raw framed stream. Buffers across calls, resyncs past noise,
    // drops an over-long (corrupt) length, and calls decode_from_radio per complete frame.
    void feed(const uint8_t* data, size_t len);

    // Per-connection reset: clears sync/burst state and sets a fresh config nonce.
    // Call before sending want_config on a new connection.
    void begin_config(uint32_t config_nonce);

    uint32_t my_node_num() const;
    void set_my_node_num(uint32_t n);   // client sets it for its own send-echo path
    bool synced() const;                // config burst completed (matching nonce)
    int  node_count() const;            // nodes in the last completed burst
    PacketCounts packet_counts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace meshtastic
