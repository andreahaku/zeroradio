/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace toolkit {

// Background reader of NMEA text (e.g. !AIVDM lines from rtl_ais / AIS-catcher)
// over the network. Two transports:
//   - Udp: bind a local UDP port and receive datagrams (rtl_ais default).
//   - Tcp: connect to host:port and read the stream (AIS-catcher, aggregators).
// Each complete line (terminated by '\n') is delivered to `on_nmea` ON THE READER
// THREAD; partial lines are buffered across reads/datagrams so a sentence split
// across the wire is never truncated. Lifecycle mirrors FileJsonSource: an atomic
// stop flag, a short socket timeout so stop is observed quickly, and a join in
// the destructor. Resilient: a UDP bind / TCP connect failure leaves ok()==false
// and retries; the UI never blocks.
class NmeaNetSource {
public:
    enum class Protocol { Udp, Tcp };

    NmeaNetSource(Protocol proto, std::string host, uint16_t port,
                  std::function<void(const std::string&)> on_nmea);
    ~NmeaNetSource();

    NmeaNetSource(const NmeaNetSource&) = delete;
    NmeaNetSource& operator=(const NmeaNetSource&) = delete;

    void start();
    void stop();

    // True while data is currently flowing (UDP datagrams arriving / TCP
    // connected and reading). Drives the UI connection indicator.
    bool ok() const;

private:
    void run();
    void run_udp();
    void run_tcp();
    // Append `n` bytes, emit every complete line to on_nmea, keep the remainder.
    void ingest(const char* data, std::size_t n);

    Protocol proto_;
    std::string host_;
    uint16_t port_;
    std::function<void(const std::string&)> on_nmea_;
    std::string buffer_;

    std::atomic<bool> running_{false};
    std::atomic<bool> ok_{false};
    std::thread thread_;
};

} // namespace toolkit
