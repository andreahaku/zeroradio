/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_decoder.h"

#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace meshtastic {
namespace {

// Meshtastic stream framing: 0x94 0xC3 then a big-endian 16-bit length, then that
// many bytes of a serialized FromRadio. A length above this is treated as corruption.
constexpr uint8_t kStart1 = 0x94;
constexpr uint8_t kStart2 = 0xC3;
constexpr uint16_t kMaxFrame = 512;

constexpr double kPositionScale = 1e-7; // Meshtastic lat/lon are fixed-point 1e-7 deg

} // namespace

struct MeshDecoder::Impl {
    DecodeSink sink;

    uint32_t nonce;                     // want_config_id echoed in config_complete_id
    std::atomic<uint32_t> my_node_num{0};
    std::atomic<bool> synced{false};
    std::atomic<int> node_count{0};
    std::atomic<int> cnt_text{0};
    std::atomic<int> cnt_nodeinfo{0};
    std::atomic<int> cnt_pos{0};
    std::atomic<int> cnt_total{0};

    int burst_nodes = 0;                // node_info seen in the current (unsynced) burst
    std::vector<uint8_t> buf;           // feed() reassembly buffer
    meshtastic_FromRadio scratch{};     // big union, kept off the stack across decodes

    Impl(DecodeSink s, uint32_t config_nonce)
        : sink(std::move(s)), nonce(config_nonce) {}

    // Map a decoded NodeInfo onto a NodeUpdate. Only sets what actually arrived —
    // Meshtastic spreads a node's data across packets.
    NodeUpdate to_node_update(const meshtastic_NodeInfo& ni) const {
        NodeUpdate u;
        if (ni.has_user && ni.user.id[0] != '\0') {
            u.id = ni.user.id;
        } else {
            char idbuf[16];
            std::snprintf(idbuf, sizeof(idbuf), "!%08x", ni.num);
            u.id = idbuf;
        }
        u.is_self = (ni.num == my_node_num.load());
        if (ni.has_user) {
            if (ni.user.long_name[0] != '\0') {
                u.has_long = true;
                u.long_name = ni.user.long_name;
            }
            if (ni.user.short_name[0] != '\0') {
                u.has_short = true;
                u.short_name = ni.user.short_name;
            }
            u.has_hw = true;
            u.hw_model = static_cast<int>(ni.user.hw_model);
            u.has_role = true;
            u.role = static_cast<int>(ni.user.role);
        }
        if (ni.has_device_metrics) {
            if (ni.device_metrics.has_battery_level) {
                u.has_battery = true;
                u.battery = static_cast<int>(ni.device_metrics.battery_level);
            }
            if (ni.device_metrics.has_voltage) {
                u.has_voltage = true;
                u.voltage = ni.device_metrics.voltage;
            }
        }
        if (ni.has_position && ni.position.has_latitude_i && ni.position.has_longitude_i) {
            u.has_pos = true;
            u.lat = ni.position.latitude_i * kPositionScale;
            u.lon = ni.position.longitude_i * kPositionScale;
        }
        u.has_snr = true;
        u.snr = ni.snr;
        if (ni.has_hops_away) {
            u.has_hops = true;
            u.hops = ni.hops_away;
        }
        if (ni.last_heard != 0) {
            u.has_last_heard = true;
            u.last_heard = ni.last_heard;
        }
        return u;
    }

    void handle_node_info(const meshtastic_NodeInfo& ni) {
        if (!synced.load()) ++burst_nodes;
        ++cnt_nodeinfo;
        const bool has_fix =
            ni.has_position && ni.position.has_latitude_i && ni.position.has_longitude_i;
        if (has_fix) ++cnt_pos;
        if (sink.on_node) sink.on_node(to_node_update(ni));
    }

    void handle_channel(const meshtastic_Channel& ch) {
        if (!sink.on_channel) return;
        ChannelUpdate u;
        u.index = ch.index;
        u.role = static_cast<int>(ch.role);
        if (ch.has_settings && ch.settings.name[0] != '\0') u.name = ch.settings.name;
        sink.on_channel(u);
    }

    void handle_config_complete(uint32_t id) {
        if (id != nonce || synced.load()) return;
        synced.store(true);
        node_count.store(burst_nodes);
        if (sink.on_config_complete) sink.on_config_complete(burst_nodes);
    }

    void handle_packet(const meshtastic_MeshPacket& pkt) {
        if (pkt.which_payload_variant != meshtastic_MeshPacket_decoded_tag) return;
        const meshtastic_Data& d = pkt.decoded;
        if (d.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            ++cnt_text;
            if (!sink.on_message) return;
            MeshMessage m;
            m.from = pkt.from;
            m.to = pkt.to;
            m.is_self = (pkt.from == my_node_num.load());
            m.channel = pkt.channel;
            m.id = pkt.id;
            m.rx_time = pkt.rx_time;
            m.text.assign(reinterpret_cast<const char*>(d.payload.bytes), d.payload.size);
            sink.on_message(m);
            return;
        }
        if (d.portnum == meshtastic_PortNum_ROUTING_APP) {
            // ACK / failure for a message we sent (matched by request_id).
            if (!sink.on_ack || d.request_id == 0) return;
            meshtastic_Routing r = meshtastic_Routing_init_zero;
            pb_istream_t is = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
            if (!pb_decode(&is, meshtastic_Routing_fields, &r)) return;
            if (r.which_variant != meshtastic_Routing_error_reason_tag) return;
            const AckState st = (r.error_reason == meshtastic_Routing_Error_NONE)
                                    ? AckState::Delivered
                                    : AckState::Failed;
            sink.on_ack(d.request_id, st);
        }
    }

    bool decode_from_radio(const uint8_t* data, size_t len) {
        scratch = meshtastic_FromRadio_init_zero;
        pb_istream_t is = pb_istream_from_buffer(data, len);
        if (!pb_decode(&is, meshtastic_FromRadio_fields, &scratch)) return false;

        ++cnt_total;
        switch (scratch.which_payload_variant) {
            case meshtastic_FromRadio_my_info_tag:
                my_node_num.store(scratch.my_info.my_node_num);
                if (sink.on_self) sink.on_self(my_node_num.load());
                break;
            case meshtastic_FromRadio_node_info_tag:
                handle_node_info(scratch.node_info);
                break;
            case meshtastic_FromRadio_channel_tag:
                handle_channel(scratch.channel);
                break;
            case meshtastic_FromRadio_config_complete_id_tag:
                handle_config_complete(scratch.config_complete_id);
                break;
            case meshtastic_FromRadio_packet_tag:
                handle_packet(scratch.packet);
                break;
            default:
                break; // other variants: ignored for now
        }
        return true;
    }

    // Pull complete frames out of buf, decoding each. Resyncs past noise, waits for a
    // partial frame, drops an over-long (corrupt) length.
    void drain() {
        for (;;) {
            while (buf.size() >= 2 && !(buf[0] == kStart1 && buf[1] == kStart2)) {
                buf.erase(buf.begin());
            }
            if (buf.size() < 4) return;
            const uint16_t len = static_cast<uint16_t>((buf[2] << 8) | buf[3]);
            if (len > kMaxFrame) {
                buf.erase(buf.begin()); // corrupt length: drop a byte and resync
                continue;
            }
            if (buf.size() < static_cast<size_t>(4) + len) return;
            decode_from_radio(buf.data() + 4, len);
            buf.erase(buf.begin(), buf.begin() + 4 + len);
        }
    }
};

MeshDecoder::MeshDecoder(DecodeSink sink, uint32_t config_nonce)
    : impl_(std::make_unique<Impl>(std::move(sink), config_nonce)) {}

MeshDecoder::~MeshDecoder() = default;

bool MeshDecoder::decode_from_radio(const uint8_t* data, size_t len) {
    return impl_->decode_from_radio(data, len);
}

void MeshDecoder::feed(const uint8_t* data, size_t len) {
    impl_->buf.insert(impl_->buf.end(), data, data + len);
    impl_->drain();
}

void MeshDecoder::begin_config(uint32_t config_nonce) {
    impl_->nonce = config_nonce;
    impl_->synced.store(false);
    impl_->node_count.store(0);
    impl_->burst_nodes = 0;
    impl_->buf.clear();
}

uint32_t MeshDecoder::my_node_num() const { return impl_->my_node_num.load(); }
void MeshDecoder::set_my_node_num(uint32_t n) { impl_->my_node_num.store(n); }
bool MeshDecoder::synced() const { return impl_->synced.load(); }
int MeshDecoder::node_count() const { return impl_->node_count.load(); }

PacketCounts MeshDecoder::packet_counts() const {
    PacketCounts c;
    c.text     = impl_->cnt_text.load();
    c.nodeinfo = impl_->cnt_nodeinfo.load();
    c.pos      = impl_->cnt_pos.load();
    c.total    = impl_->cnt_total.load();
    return c;
}

} // namespace meshtastic
