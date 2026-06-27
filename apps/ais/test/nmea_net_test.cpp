/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 *
 * Loopback integration test for the live NMEA network source: bind a UDP
 * NmeaNetSource on localhost, send it one !AIVDM datagram, and verify the source
 * delivers the complete line and that it decodes to the expected MMSI. Hardware-
 * free (pure localhost UDP), so it runs in CI alongside the decoder unit test.
 */

#include "ais_decoder.h"
#include "nmea_net_source.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr uint16_t kPort = 24601;
// Canonical type-1 sentence (gpsd example), MMSI 477553000.
const char* kLine = "!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C\r\n";
} // namespace

int main() {
    std::mutex mtx;
    std::string got;
    std::atomic<bool> received{false};

    toolkit::NmeaNetSource src(toolkit::NmeaNetSource::Protocol::Udp, "", kPort,
                               [&](const std::string& s) {
                                   std::lock_guard<std::mutex> lk(mtx);
                                   got += s;
                                   received.store(true);
                               });
    src.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250)); // let the bind settle

    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(fd, kLine, std::strlen(kLine), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    close(fd);

    for (int i = 0; i < 40 && !received.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    src.stop();

    int fails = 0;
    if (!received.load()) {
        std::printf("  FAIL: no datagram delivered\n");
        ++fails;
    } else {
        std::string line;
        {
            std::lock_guard<std::mutex> lk(mtx);
            line = got;
        }
        // The source delivers complete lines (with the trailing newline); take
        // the first and strip CR/LF, then decode it.
        const auto nl = line.find('\n');
        std::string first = nl == std::string::npos ? line : line.substr(0, nl);
        while (!first.empty() && (first.back() == '\r' || first.back() == '\n')) {
            first.pop_back();
        }
        ais::Vessel v;
        if (!ais::parse_aivdm(first, v) || v.mmsi != 477553000u) {
            std::printf("  FAIL: decoded mmsi=%u (want 477553000)\n", v.mmsi);
            ++fails;
        }
    }

    if (fails == 0) {
        std::printf("NMEA UDP source: 1/1 OK\n");
        return 0;
    }
    std::printf("NMEA net test: %d failure(s)\n", fails);
    return 1;
}
