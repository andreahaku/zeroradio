/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_client_source.h"

#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace meshtastic {
namespace {

// Meshtastic stream framing: 0x94 0xC3 then a big-endian 16-bit length, then that
// many bytes of a serialized ToRadio/FromRadio. >512 is treated as corruption.
constexpr uint8_t kStart1 = 0x94;
constexpr uint8_t kStart2 = 0xC3;
constexpr uint16_t kMaxFrame = 512;

constexpr int kPollMs = 100;            // read poll quantum (fast stop)
constexpr auto kHeartbeat = std::chrono::seconds(30);

bool write_all(int fd, const uint8_t* p, size_t n) {
    size_t off = 0;
    while (off < n) {
        const ssize_t w = ::send(fd, p + off, n - off, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(w);
    }
    return true;
}

} // namespace

struct MeshtasticClientSource::Impl {
    std::string host;
    uint16_t port;
    Callbacks cb;

    std::atomic<bool> running{false};
    std::atomic<bool> ok{false};
    std::atomic<int> node_count{0};
    std::thread thread;

    uint32_t nonce = 0x1000;
    std::atomic<uint32_t> my_node_num{0};

    // Outbound TX queue (UI thread enqueues, reader loop writes).
    std::mutex out_mutex;
    std::deque<std::vector<uint8_t>> outbox;
    std::atomic<uint32_t> next_id{1};

    // Reused across decodes: FromRadio is a big union, keep it off the stack.
    meshtastic_FromRadio scratch{};

    Impl(std::string h, uint16_t p, Callbacks c)
        : host(std::move(h)), port(p), cb(std::move(c)) {}

    int connect_once() {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            ::close(fd);
            return -1;
        }
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    bool send_toradio(int fd, const meshtastic_ToRadio& msg) {
        uint8_t payload[kMaxFrame];
        pb_ostream_t os = pb_ostream_from_buffer(payload, sizeof(payload));
        if (!pb_encode(&os, meshtastic_ToRadio_fields, &msg)) return false;
        const size_t len = os.bytes_written;
        const uint8_t hdr[4] = {kStart1, kStart2,
                                static_cast<uint8_t>((len >> 8) & 0xff),
                                static_cast<uint8_t>(len & 0xff)};
        return write_all(fd, hdr, 4) && write_all(fd, payload, len);
    }

    bool send_want_config(int fd) {
        meshtastic_ToRadio t = meshtastic_ToRadio_init_zero;
        t.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
        t.want_config_id = nonce;
        return send_toradio(fd, t);
    }

    bool send_heartbeat(int fd) {
        meshtastic_ToRadio t = meshtastic_ToRadio_init_zero;
        t.which_payload_variant = meshtastic_ToRadio_heartbeat_tag;
        t.heartbeat = meshtastic_Heartbeat_init_zero;
        return send_toradio(fd, t);
    }

    static bool encode_frame(const meshtastic_ToRadio& msg, std::vector<uint8_t>& out) {
        uint8_t payload[kMaxFrame];
        pb_ostream_t os = pb_ostream_from_buffer(payload, sizeof(payload));
        if (!pb_encode(&os, meshtastic_ToRadio_fields, &msg)) return false;
        const size_t len = os.bytes_written;
        out.clear();
        out.reserve(4 + len);
        out.push_back(kStart1);
        out.push_back(kStart2);
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
        out.push_back(static_cast<uint8_t>(len & 0xff));
        out.insert(out.end(), payload, payload + len);
        return true;
    }

    uint32_t enqueue_text(const std::string& text, uint32_t to, uint8_t channel,
                          bool want_ack) {
        const uint32_t id = next_id.fetch_add(1);
        meshtastic_ToRadio t = meshtastic_ToRadio_init_zero;
        t.which_payload_variant = meshtastic_ToRadio_packet_tag;
        meshtastic_MeshPacket& p = t.packet;
        p.from = my_node_num.load();
        p.to = to;
        p.channel = channel;
        p.id = id;
        p.want_ack = want_ack;
        p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        size_t n = text.size();
        if (n > sizeof(p.decoded.payload.bytes)) n = sizeof(p.decoded.payload.bytes);
        p.decoded.payload.size = static_cast<pb_size_t>(n);
        std::memcpy(p.decoded.payload.bytes, text.data(), n);

        std::vector<uint8_t> frame;
        if (encode_frame(t, frame)) {
            std::lock_guard<std::mutex> lk(out_mutex);
            outbox.push_back(std::move(frame));
        }
        // Echo to our own feed immediately (optimistic; ACK state lands later).
        if (cb.on_message) {
            MeshMessage m;
            m.from = my_node_num.load();
            m.to = to;
            m.is_self = true;
            m.channel = channel;
            m.text.assign(text.data(), n);
            m.id = id;
            m.ack = want_ack ? AckState::Pending : AckState::None;
            cb.on_message(m);
        }
        return id;
    }

    enum class Frame { NeedMore, Skipped, Decoded };

    // Pull one frame from `buf` (consuming it). Resyncs past noise, waits for a
    // partial frame, drops an over-long (corrupt) length.
    Frame next_frame(std::vector<uint8_t>& buf) {
        // Resync to the start marker.
        while (buf.size() >= 2 && !(buf[0] == kStart1 && buf[1] == kStart2)) {
            buf.erase(buf.begin());
        }
        if (buf.size() < 4) return Frame::NeedMore;
        const uint16_t len = static_cast<uint16_t>((buf[2] << 8) | buf[3]);
        if (len > kMaxFrame) {
            buf.erase(buf.begin()); // corrupt length: drop a byte and resync
            return Frame::Skipped;
        }
        if (buf.size() < static_cast<size_t>(4) + len) return Frame::NeedMore;
        scratch = meshtastic_FromRadio_init_zero;
        pb_istream_t is = pb_istream_from_buffer(buf.data() + 4, len);
        const bool decoded = pb_decode(&is, meshtastic_FromRadio_fields, &scratch);
        buf.erase(buf.begin(), buf.begin() + 4 + len);
        return decoded ? Frame::Decoded : Frame::Skipped;
    }

    void handle(bool& synced, int& burst_nodes) {
        switch (scratch.which_payload_variant) {
            case meshtastic_FromRadio_my_info_tag:
                my_node_num.store(scratch.my_info.my_node_num);
                std::fprintf(stderr,
                             "[meshtastic] my_info: node_num=0x%08x nodedb=%u\n",
                             my_node_num.load(), scratch.my_info.nodedb_count);
                if (cb.on_self) cb.on_self(my_node_num.load());
                break;
            case meshtastic_FromRadio_node_info_tag: {
                if (!synced) ++burst_nodes;
                const meshtastic_NodeInfo& ni = scratch.node_info;
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
                if (ni.has_position && ni.position.has_latitude_i &&
                    ni.position.has_longitude_i) {
                    u.has_pos = true;
                    u.lat = ni.position.latitude_i * 1e-7;
                    u.lon = ni.position.longitude_i * 1e-7;
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
                if (cb.on_node) cb.on_node(u);
                break;
            }
            case meshtastic_FromRadio_channel_tag: {
                const meshtastic_Channel& ch = scratch.channel;
                if (cb.on_channel) {
                    ChannelUpdate u;
                    u.index = ch.index;
                    u.role = static_cast<int>(ch.role);
                    if (ch.has_settings && ch.settings.name[0] != '\0') u.name = ch.settings.name;
                    cb.on_channel(u);
                }
                break;
            }
            case meshtastic_FromRadio_config_complete_id_tag:
                if (scratch.config_complete_id == nonce && !synced) {
                    synced = true;
                    node_count.store(burst_nodes);
                    ok.store(true);
                    std::fprintf(stderr, "[meshtastic] config complete: %d nodes\n",
                                 burst_nodes);
                    if (cb.on_config_complete) cb.on_config_complete(burst_nodes);
                }
                break;
            case meshtastic_FromRadio_packet_tag: {
                const meshtastic_MeshPacket& pkt = scratch.packet;
                if (pkt.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
                    break;
                }
                const meshtastic_Data& d = pkt.decoded;
                if (d.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
                    MeshMessage m;
                    m.from = pkt.from;
                    m.to = pkt.to;
                    m.is_self = (pkt.from == my_node_num.load());
                    m.channel = pkt.channel;
                    m.id = pkt.id;
                    m.rx_time = pkt.rx_time;
                    m.text.assign(reinterpret_cast<const char*>(d.payload.bytes),
                                  d.payload.size);
                    if (cb.on_message) cb.on_message(m);
                } else if (d.portnum == meshtastic_PortNum_ROUTING_APP) {
                    // ACK / failure for a message we sent (matched by request_id).
                    meshtastic_Routing r = meshtastic_Routing_init_zero;
                    pb_istream_t is = pb_istream_from_buffer(d.payload.bytes, d.payload.size);
                    if (pb_decode(&is, meshtastic_Routing_fields, &r) &&
                        r.which_variant == meshtastic_Routing_error_reason_tag &&
                        cb.on_ack && d.request_id != 0) {
                        const AckState st = (r.error_reason == meshtastic_Routing_Error_NONE)
                                                ? AckState::Delivered
                                                : AckState::Failed;
                        cb.on_ack(d.request_id, st);
                    }
                }
                break;
            }
            default:
                break; // other variants: ignored for now
        }
    }

    void backoff() {
        // Up to ~2 s, observing stop in short steps.
        for (int i = 0; i < 20 && running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void run() {
        std::vector<uint8_t> buf;
        while (running.load()) {
            const int fd = connect_once();
            if (fd < 0) {
                ok.store(false);
                backoff();
                continue;
            }
            std::fprintf(stderr, "[meshtastic] connected %s:%u\n", host.c_str(), port);
            ++nonce; // fresh request nonce per connection
            send_want_config(fd);

            bool synced = false;
            int burst_nodes = 0;
            buf.clear();
            auto last_hb = std::chrono::steady_clock::now();

            while (running.load()) {
                pollfd p{fd, POLLIN, 0};
                const int pr = ::poll(&p, 1, kPollMs);
                if (pr < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                if (pr > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR))) {
                    uint8_t tmp[1024];
                    const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
                    if (n <= 0) break; // peer closed / error
                    buf.insert(buf.end(), tmp, tmp + n);
                    for (;;) {
                        const Frame r = next_frame(buf);
                        if (r == Frame::NeedMore) break;
                        if (r == Frame::Decoded) handle(synced, burst_nodes);
                    }
                }

                // Drain queued outbound frames (send_text from the UI thread).
                {
                    std::deque<std::vector<uint8_t>> pending;
                    {
                        std::lock_guard<std::mutex> lk(out_mutex);
                        pending.swap(outbox);
                    }
                    bool werr = false;
                    for (const auto& f : pending) {
                        if (!write_all(fd, f.data(), f.size())) {
                            werr = true;
                            break;
                        }
                    }
                    if (werr) break;
                }

                const auto now = std::chrono::steady_clock::now();
                if (synced && now - last_hb > kHeartbeat) {
                    if (!send_heartbeat(fd)) break;
                    last_hb = now;
                }
            }

            ::close(fd);
            ok.store(false);
            if (running.load()) backoff();
        }
    }
};

MeshtasticClientSource::MeshtasticClientSource(std::string host, uint16_t port, Callbacks cb)
    : impl_(std::make_unique<Impl>(std::move(host), port, std::move(cb))) {}

MeshtasticClientSource::~MeshtasticClientSource() {
    stop();
}

void MeshtasticClientSource::start() {
    if (impl_->running.exchange(true)) return; // already running
    impl_->thread = std::thread([this] { impl_->run(); });
}

void MeshtasticClientSource::stop() {
    impl_->running.store(false);
    if (impl_->thread.joinable()) impl_->thread.join();
}

bool MeshtasticClientSource::ok() const {
    return impl_->ok.load();
}

int MeshtasticClientSource::node_count() const {
    return impl_->node_count.load();
}

uint32_t MeshtasticClientSource::send_text(const std::string& text, uint32_t to,
                                           uint8_t channel, bool want_ack) {
    return impl_->enqueue_text(text, to, channel, want_ack);
}

} // namespace meshtastic
