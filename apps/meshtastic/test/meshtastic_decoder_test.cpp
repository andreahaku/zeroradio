/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 *
 * Frozen parity test for the pure Meshtastic FromRadio decoder (apps/meshtastic
 * src/decode/meshtastic_decoder). Every vector is a real protobuf frame; the
 * expected values (in meshtastic_decoder_vectors.inc) were agreed by two independent
 * protobuf implementations — the C++ nanopb decoder under test and the python
 * meshtastic lib that captured/encoded the bytes (see scratchpad/gen_vectors.py),
 * mirroring the AIS gpsdecode+pyais oracle. This file is the immutable reward: it is
 * not edited to make code pass.
 *
 * Coverage (existing decode surface, per the FromRadio union + handled portnums):
 *   my_info -> on_self | node_info(self capt + remote synth) -> on_node
 *   channel(primary/disabled capt + named synth) -> on_channel
 *   config_complete (matching nonce) -> on_config_complete/synced
 *   TEXT_MESSAGE_APP -> on_message | ROUTING_APP(NONE/NO_RESPONSE) -> on_ack
 *   invalid protobuf -> decode returns false | framed stream w/ noise -> feed() resync
 */

#include "meshtastic_decoder.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

#include "meshtastic_decoder_vectors.inc"

namespace {

int g_fails = 0;

void check(bool ok, const char* name, const char* field) {
    if (!ok) {
        std::printf("  FAIL %-20s %s\n", name, field);
        ++g_fails;
    }
}

bool dclose(double a, double b) { return std::fabs(a - b) <= 1e-6; }

} // namespace

using namespace meshtastic;

int main() {
    // --- on_self (my_info) ---
    for (const auto& e : kSelf) {
        std::optional<uint32_t> got;
        DecodeSink s;
        s.on_self = [&](uint32_t n) { got = n; };
        MeshDecoder dec(s, kConfigNonce);
        const bool ok = dec.decode_from_radio(e.d, e.n);
        check(ok, e.name, "decode returned false");
        check(got.has_value(), e.name, "on_self not fired");
        if (got) check(*got == e.my_node_num, e.name, "my_node_num");
        check(dec.my_node_num() == e.my_node_num, e.name, "my_node_num() getter");
    }

    // --- on_node (NodeInfo) ---
    for (const auto& e : kNode) {
        std::optional<NodeUpdate> got;
        DecodeSink s;
        s.on_node = [&](const NodeUpdate& u) { got = u; };
        MeshDecoder dec(s, kConfigNonce);
        dec.set_my_node_num(0xBEEF1234u); // so is_self matches the captured self node
        const bool ok = dec.decode_from_radio(e.d, e.n);
        check(ok, e.name, "decode returned false");
        if (!got) { check(false, e.name, "on_node not fired"); continue; }
        const NodeUpdate& u = *got;
        check(u.id == e.id, e.name, "id");
        check(u.is_self == e.is_self, e.name, "is_self");
        check(u.has_long == e.has_long, e.name, "has_long");
        if (e.has_long) check(u.long_name == e.long_name, e.name, "long_name");
        check(u.has_short == e.has_short, e.name, "has_short");
        if (e.has_short) check(u.short_name == e.short_name, e.name, "short_name");
        check(u.has_pos == e.has_pos, e.name, "has_pos");
        if (e.has_pos) {
            check(dclose(u.lat, e.lat), e.name, "lat");
            check(dclose(u.lon, e.lon), e.name, "lon");
        }
        check(u.has_snr == e.has_snr, e.name, "has_snr");
        if (e.has_snr) check(dclose(u.snr, e.snr), e.name, "snr");
        check(u.has_hops == e.has_hops, e.name, "has_hops");
        if (e.has_hops) check(u.hops == e.hops, e.name, "hops");
        check(u.has_last_heard == e.has_last, e.name, "has_last_heard");
        if (e.has_last) check(u.last_heard == e.last_heard, e.name, "last_heard");
        check(u.has_hw == e.has_hw, e.name, "has_hw");
        if (e.has_hw) check(u.hw_model == e.hw, e.name, "hw_model");
        check(u.has_role == e.has_role, e.name, "has_role");
        if (e.has_role) check(u.role == e.role, e.name, "role");
        check(u.has_battery == e.has_batt, e.name, "has_battery");
        if (e.has_batt) check(u.battery == e.batt, e.name, "battery");
        check(u.has_voltage == e.has_volt, e.name, "has_voltage");
        if (e.has_volt) check(dclose(u.voltage, e.volt), e.name, "voltage");
        const PacketCounts pc = dec.packet_counts();
        check(pc.nodeinfo == 1 && pc.total == 1, e.name, "nodeinfo/total counters");
        check(pc.pos == (e.has_pos ? 1 : 0), e.name, "pos counter");
    }

    // --- on_channel ---
    for (const auto& e : kChannel) {
        std::optional<ChannelUpdate> got;
        DecodeSink s;
        s.on_channel = [&](const ChannelUpdate& c) { got = c; };
        MeshDecoder dec(s, kConfigNonce);
        const bool ok = dec.decode_from_radio(e.d, e.n);
        check(ok, e.name, "decode returned false");
        if (!got) { check(false, e.name, "on_channel not fired"); continue; }
        check(got->index == e.index, e.name, "index");
        check(got->role == e.role, e.name, "role");
        check(got->name == e.chname, e.name, "name");
    }

    // --- on_message (TEXT_MESSAGE_APP) ---
    for (const auto& e : kMessage) {
        std::optional<MeshMessage> got;
        DecodeSink s;
        s.on_message = [&](const MeshMessage& m) { got = m; };
        MeshDecoder dec(s, kConfigNonce);
        dec.set_my_node_num(0xBEEF1234u);
        const bool ok = dec.decode_from_radio(e.d, e.n);
        check(ok, e.name, "decode returned false");
        if (!got) { check(false, e.name, "on_message not fired"); continue; }
        const MeshMessage& m = *got;
        check(m.from == e.from, e.name, "from");
        check(m.to == e.to, e.name, "to");
        check(m.is_self == e.is_self, e.name, "is_self");
        check(m.channel == e.channel, e.name, "channel");
        check(m.id == e.id, e.name, "id");
        check(m.rx_time == e.rx, e.name, "rx_time");
        check(m.text == e.text, e.name, "text");
        const PacketCounts pc = dec.packet_counts();
        check(pc.text == 1 && pc.total == 1, e.name, "text/total counters");
    }

    // --- on_ack (ROUTING_APP) ---
    for (const auto& e : kAck) {
        std::optional<uint32_t> req;
        std::optional<AckState> st;
        DecodeSink s;
        s.on_ack = [&](uint32_t r, AckState a) { req = r; st = a; };
        MeshDecoder dec(s, kConfigNonce);
        const bool ok = dec.decode_from_radio(e.d, e.n);
        check(ok, e.name, "decode returned false");
        check(req.has_value() && st.has_value(), e.name, "on_ack not fired");
        if (req) check(*req == e.req_id, e.name, "request_id");
        if (st) check(static_cast<int>(*st) == e.ack_state, e.name, "ack_state");
    }

    // --- unhandled variant/portnum: decode succeeds but fires NO callback (bumps total) ---
    for (const auto& e : kIgnored) {
        bool any = false;
        DecodeSink s;
        s.on_self = [&](uint32_t) { any = true; };
        s.on_node = [&](const NodeUpdate&) { any = true; };
        s.on_message = [&](const MeshMessage&) { any = true; };
        s.on_ack = [&](uint32_t, AckState) { any = true; };
        s.on_channel = [&](const ChannelUpdate&) { any = true; };
        s.on_config_complete = [&](int) { any = true; };
        MeshDecoder dec(s, kConfigNonce);
        const bool ok = dec.decode_from_radio(e.d, e.n);
        check(ok, e.name, "unhandled frame should still decode true");
        check(!any, e.name, "no callback on unhandled frame");
        check(dec.packet_counts().total == 1, e.name, "total counter bumped");
    }

    // --- invalid protobuf must be rejected (decode returns false, no callback) ---
    for (const auto& e : kReject) {
        bool any = false;
        DecodeSink s;
        s.on_self = [&](uint32_t) { any = true; };
        s.on_node = [&](const NodeUpdate&) { any = true; };
        s.on_message = [&](const MeshMessage&) { any = true; };
        s.on_ack = [&](uint32_t, AckState) { any = true; };
        s.on_channel = [&](const ChannelUpdate&) { any = true; };
        s.on_config_complete = [&](int) { any = true; };
        MeshDecoder dec(s, kConfigNonce);
        const bool ok = dec.decode_from_radio(e.d, e.n);
        check(!ok, e.name, "invalid protobuf should return false");
        check(!any, e.name, "no callback on invalid frame");
    }

    // --- config_complete sequence: feed node_info x2 then the matching config_done ---
    {
        int done_nodes = -1;
        int nodes_seen = 0;
        DecodeSink s;
        s.on_node = [&](const NodeUpdate&) { ++nodes_seen; };
        s.on_config_complete = [&](int n) { done_nodes = n; };
        MeshDecoder dec(s, kConfigNonce);
        dec.begin_config(kConfigNonce);
        for (const auto& e : kNode) dec.decode_from_radio(e.d, e.n);
        check(!dec.synced(), "config_seq", "should not be synced mid-burst");
        const bool ok = dec.decode_from_radio(V_config_done_p, V_config_done_n);
        check(ok, "config_seq", "config_done decode false");
        check(dec.synced(), "config_seq", "synced() should be true after config_done");
        check(done_nodes == kBurstNodes, "config_seq", "on_config_complete node count");
        check(dec.node_count() == kBurstNodes, "config_seq", "node_count() getter");
        check(nodes_seen == kBurstNodes, "config_seq", "node callbacks during burst");
    }

    // --- mismatched nonce: config_complete_id != our nonce must NOT complete the burst ---
    {
        bool done = false;
        DecodeSink s;
        s.on_config_complete = [&](int) { done = true; };
        MeshDecoder dec(s, kConfigNonce + 1u); // wrong nonce
        const bool ok = dec.decode_from_radio(V_config_done_p, V_config_done_n);
        check(ok, "nonce_mismatch", "config_done should decode");
        check(!done, "nonce_mismatch", "on_config_complete must not fire on wrong nonce");
        check(!dec.synced(), "nonce_mismatch", "must not be synced on wrong nonce");
    }

    // --- begin_config resets sync/burst state between connections ---
    {
        int done_count = 0;
        DecodeSink s;
        s.on_config_complete = [&](int) { ++done_count; };
        MeshDecoder dec(s, kConfigNonce);
        dec.decode_from_radio(kNode[0].d, kNode[0].n);
        dec.decode_from_radio(V_config_done_p, V_config_done_n); // synced, burst=1
        check(dec.synced() && dec.node_count() == 1, "reset", "first burst synced");
        dec.begin_config(kConfigNonce);                          // reconnect: reset
        check(!dec.synced(), "reset", "begin_config clears synced");
        dec.decode_from_radio(V_config_done_p, V_config_done_n); // second burst, 0 nodes
        check(dec.node_count() == 0, "reset", "burst count reset to 0");
        check(done_count == 2, "reset", "config_complete fired once per burst");
    }

    // --- feed(): an over-long (corrupt) frame length is dropped, then resync + decode ---
    {
        std::optional<uint32_t> got;
        DecodeSink s;
        s.on_self = [&](uint32_t n) { got = n; };
        MeshDecoder dec(s, kConfigNonce);
        // 0x94 0xC3 0xFF 0xFF = len 65535 (> 512, corrupt) then a valid framed my_info.
        std::vector<uint8_t> stream = {0x94, 0xC3, 0xFF, 0xFF};
        stream.push_back(0x94);
        stream.push_back(0xC3);
        stream.push_back(static_cast<uint8_t>((sizeof(V_my_info) >> 8) & 0xFF));
        stream.push_back(static_cast<uint8_t>(sizeof(V_my_info) & 0xFF));
        stream.insert(stream.end(), V_my_info, V_my_info + sizeof(V_my_info));
        dec.feed(stream.data(), stream.size());
        check(got.has_value() && *got == 3203338804u, "feed_overlong", "resync past corrupt length");
    }

    // --- feed(): framed stream with leading noise must resync + decode the frame ---
    {
        std::optional<uint32_t> got;
        DecodeSink s;
        s.on_self = [&](uint32_t n) { got = n; };
        MeshDecoder dec(s, kConfigNonce);
        // noise, then 0x94 0xC3 <len16> <V_my_info>
        std::vector<uint8_t> stream = {0x00, 0xFF, 0x11};
        stream.push_back(0x94);
        stream.push_back(0xC3);
        stream.push_back(static_cast<uint8_t>((sizeof(V_my_info) >> 8) & 0xFF));
        stream.push_back(static_cast<uint8_t>(sizeof(V_my_info) & 0xFF));
        stream.insert(stream.end(), V_my_info, V_my_info + sizeof(V_my_info));
        // feed in two chunks to exercise cross-call buffering
        dec.feed(stream.data(), 5);
        dec.feed(stream.data() + 5, stream.size() - 5);
        check(got.has_value() && *got == 3203338804u, "feed_resync", "framed my_info via feed()");
    }

    const int total = static_cast<int>(
        (sizeof(kSelf) / sizeof(kSelf[0])) + (sizeof(kNode) / sizeof(kNode[0])) +
        (sizeof(kChannel) / sizeof(kChannel[0])) + (sizeof(kMessage) / sizeof(kMessage[0])) +
        (sizeof(kAck) / sizeof(kAck[0])) + (sizeof(kIgnored) / sizeof(kIgnored[0])) +
        (sizeof(kReject) / sizeof(kReject[0])));
    if (g_fails == 0) {
        std::printf("Meshtastic decoder: %d vectors + nonce/reset/feed state checks OK\n", total);
        return 0;
    }
    std::printf("Meshtastic decoder: %d assertion(s) FAILED\n", g_fails);
    return 1;
}
