/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "nmea_net_source.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace toolkit {
namespace {

constexpr std::size_t kMaxBuffer = 64 * 1024; // bound a stream with no newlines
constexpr int kRecvTimeoutMs = 200;           // so stop() is observed promptly

void set_recv_timeout(int fd, int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

} // namespace

NmeaNetSource::NmeaNetSource(Protocol proto, std::string host, uint16_t port,
                             std::function<void(const std::string&)> on_nmea)
    : proto_(proto), host_(std::move(host)), port_(port), on_nmea_(std::move(on_nmea)) {}

NmeaNetSource::~NmeaNetSource() {
    stop();
}

void NmeaNetSource::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread(&NmeaNetSource::run, this);
}

void NmeaNetSource::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    ok_.store(false);
}

bool NmeaNetSource::ok() const {
    return ok_.load();
}

void NmeaNetSource::run() {
    if (proto_ == Protocol::Udp) {
        run_udp();
    } else {
        run_tcp();
    }
}

void NmeaNetSource::ingest(const char* data, std::size_t n) {
    buffer_.append(data, n);
    const auto last_nl = buffer_.find_last_of('\n');
    if (last_nl != std::string::npos) {
        on_nmea_(buffer_.substr(0, last_nl + 1));
        buffer_.erase(0, last_nl + 1);
    }
    if (buffer_.size() > kMaxBuffer) {
        buffer_.clear(); // garbage stream with no line breaks: don't grow unbounded
    }
}

void NmeaNetSource::run_udp() {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    set_recv_timeout(fd, kRecvTimeoutMs);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = host_.empty() ? htonl(INADDR_ANY) : inet_addr(host_.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return; // ok() stays false
    }

    std::vector<char> buf(2048);
    while (running_.load()) {
        const ssize_t n = recv(fd, buf.data(), buf.size(), 0);
        if (n > 0) {
            ok_.store(true);
            ingest(buf.data(), static_cast<std::size_t>(n));
        }
        // n <= 0 is a timeout (loop re-checks running_) or a benign error.
    }
    close(fd);
}

void NmeaNetSource::run_tcp() {
    while (running_.load()) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr =
            host_.empty() ? htonl(INADDR_LOOPBACK) : inet_addr(host_.c_str());

        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close(fd);
            ok_.store(false);
            // Back off ~1s in short steps so stop() is still observed quickly.
            for (int i = 0; i < 5 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            continue;
        }

        set_recv_timeout(fd, kRecvTimeoutMs);
        ok_.store(true);
        std::vector<char> buf(2048);
        while (running_.load()) {
            const ssize_t n = recv(fd, buf.data(), buf.size(), 0);
            if (n > 0) {
                ingest(buf.data(), static_cast<std::size_t>(n));
            } else if (n == 0) {
                break; // peer closed; reconnect
            }
            // n < 0: timeout -> re-check running_ and read again.
        }
        close(fd);
        ok_.store(false);
    }
}

} // namespace toolkit
