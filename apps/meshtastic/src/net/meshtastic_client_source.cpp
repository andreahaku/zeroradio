/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_client_source.h"

#include "meshtastic_decoder.h"

#include "meshtastic/mesh.pb.h"
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
// many bytes of a serialized ToRadio. Frame decode lives in MeshDecoder; only the
// TX (encode) side needs the framing here.
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
    std::thread thread;

    uint32_t nonce = 0x1000;

    // Outbound TX queue (UI thread enqueues, reader loop writes).
    std::mutex out_mutex;
    std::deque<std::vector<uint8_t>> outbox;
    std::atomic<uint32_t> next_id{1};

    // Pure decode of the inbound FromRadio stream. Its sink forwards to cb (and flips
    // ok on config-complete); the reader thread pumps recv() bytes into decoder.feed().
    std::unique_ptr<MeshDecoder> decoder;

    Impl(std::string h, uint16_t p, Callbacks c)
        : host(std::move(h)), port(p), cb(std::move(c)) {
        DecodeSink sink;
        sink.on_self = [this](uint32_t n) { if (cb.on_self) cb.on_self(n); };
        sink.on_config_complete = [this](int nodes) {
            ok.store(true);
            std::fprintf(stderr, "[meshtastic] config complete: %d nodes\n", nodes);
            if (cb.on_config_complete) cb.on_config_complete(nodes);
        };
        sink.on_node = [this](const NodeUpdate& u) { if (cb.on_node) cb.on_node(u); };
        sink.on_message = [this](const MeshMessage& m) { if (cb.on_message) cb.on_message(m); };
        sink.on_ack = [this](uint32_t id, AckState st) { if (cb.on_ack) cb.on_ack(id, st); };
        sink.on_channel = [this](const ChannelUpdate& u) { if (cb.on_channel) cb.on_channel(u); };
        decoder = std::make_unique<MeshDecoder>(std::move(sink), nonce);
    }

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
        const uint32_t self = decoder->my_node_num();
        meshtastic_ToRadio t = meshtastic_ToRadio_init_zero;
        t.which_payload_variant = meshtastic_ToRadio_packet_tag;
        meshtastic_MeshPacket& p = t.packet;
        p.from = self;
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
            m.from = self;
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

    void backoff() {
        // Up to ~2 s, observing stop in short steps.
        for (int i = 0; i < 20 && running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void run() {
        while (running.load()) {
            const int fd = connect_once();
            if (fd < 0) {
                ok.store(false);
                backoff();
                continue;
            }
            std::fprintf(stderr, "[meshtastic] connected %s:%u\n", host.c_str(), port);
            ++nonce; // fresh request nonce per connection
            decoder->begin_config(nonce);
            send_want_config(fd);

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
                    decoder->feed(tmp, static_cast<size_t>(n));
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
                if (decoder->synced() && now - last_hb > kHeartbeat) {
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
    return impl_->decoder->node_count();
}

uint32_t MeshtasticClientSource::send_text(const std::string& text, uint32_t to,
                                           uint8_t channel, bool want_ack) {
    return impl_->enqueue_text(text, to, channel, want_ack);
}

PacketCounts MeshtasticClientSource::packet_counts() const {
    return impl_->decoder->packet_counts();
}

} // namespace meshtastic
