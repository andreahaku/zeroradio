/*
 * SPDX-License-Identifier: MIT
 *
 * remote-fb mock streamer: stands in for the device-side app so the viewer and
 * the wire protocol can be exercised on the desktop with NO LVGL and NO Pi.
 * Plays the APP role (server): listens, accepts one viewer, sends HELLO, then
 * streams an animated 320x170 RGB565 test pattern and prints any keys the viewer
 * forwards back.
 *
 *   remote-fb-mock [port=5800] [width=320] [height=170]
 */
#include "remote_fb_proto.h"

#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>
#include <vector>

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

int main(int argc, char** argv) {
    const int port = (argc >= 2) ? std::atoi(argv[1]) : REMOTE_FB_DEFAULT_PORT;
    const int W = (argc >= 3) ? std::atoi(argv[2]) : 320;
    const int H = (argc >= 4) ? std::atoi(argv[3]) : 170;

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(srv, 1) != 0) {
        std::perror("bind/listen");
        return 1;
    }
    std::printf("remote-fb-mock: listening on :%d (%dx%d). Connect the viewer.\n", port, W, H);

    int cli = accept(srv, nullptr, nullptr);
    if (cli < 0) {
        std::perror("accept");
        return 1;
    }
    std::printf("remote-fb-mock: viewer connected.\n");

    remote_fb_hello hello{};
    hello.type = REMOTE_FB_MSG_HELLO;
    hello.magic = REMOTE_FB_MAGIC;
    hello.version = REMOTE_FB_VERSION;
    hello.width = (uint16_t)W;
    hello.height = (uint16_t)H;
    hello.format = REMOTE_FB_FMT_RGB565;
    if (remote_fb_send_all(cli, &hello, sizeof(hello)) != 0) {
        std::fprintf(stderr, "remote-fb-mock: HELLO failed\n");
        return 1;
    }

    std::atomic<bool> running{true};
    // Reader thread: print forwarded keys (proves the back-channel).
    std::thread reader([&]() {
        while (running.load()) {
            uint8_t type = 0;
            if (remote_fb_recv_all(cli, &type, 1) != 0) break;
            if (type == REMOTE_FB_MSG_KEY) {
                remote_fb_key k{};
                k.type = type;
                if (remote_fb_recv_all(cli, reinterpret_cast<uint8_t*>(&k) + 1, sizeof(k) - 1) != 0) break;
                std::printf("remote-fb-mock: key %u %s\n", k.key, k.pressed ? "down" : "up");
                std::fflush(stdout);
            }
        }
        running.store(false);
    });

    std::vector<uint16_t> frame(static_cast<size_t>(W) * H);
    int t = 0;
    while (running.load()) {
        // Animated pattern: vertical gradient + a moving bright bar + a corner
        // marker, so wrong stride / wrong format / frozen stream are all obvious.
        const int bar = t % W;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                int r = (x * 255) / W;
                int g = (y * 255) / H;
                int b = (t * 3) & 0xFF;
                if (x == bar || x == (bar + 1) % W) { r = g = b = 255; }
                if (x < 8 && y < 8) { r = 255; g = 0; b = 0; } // top-left red marker
                frame[static_cast<size_t>(y) * W + x] = rgb565(r, g, b);
            }
        }
        remote_fb_frame f{};
        f.type = REMOTE_FB_MSG_FRAME;
        f.x = 0; f.y = 0; f.w = (uint16_t)W; f.h = (uint16_t)H;
        if (remote_fb_send_all(cli, &f, sizeof(f)) != 0 ||
            remote_fb_send_all(cli, frame.data(), frame.size() * 2) != 0) {
            break;
        }
        ++t;
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // ~20 fps
    }

    running.store(false);
    shutdown(cli, SHUT_RDWR);
    if (reader.joinable()) reader.join();
    close(cli);
    close(srv);
    std::printf("remote-fb-mock: viewer disconnected, exiting.\n");
    return 0;
}
