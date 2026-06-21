/*
 * SPDX-License-Identifier: MIT
 *
 * Headless end-to-end self-test for the remote-fb wire protocol (no SDL, no
 * display): acts as the viewer/client against remote-fb-mock, validates HELLO,
 * reads a few frames, checks payload sizes and the known top-left red marker,
 * forwards a key, and prints PASS/FAIL. Used in CI / quick local verification.
 *
 *   remote-fb-mock 5800 &        # the app role (server)
 *   remote-fb-selftest 127.0.0.1 5800
 */
#include "remote_fb_proto.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#include <vector>

int main(int argc, char** argv) {
    const char* host = (argc >= 2) ? argv[1] : "127.0.0.1";
    const char* port = (argc >= 3) ? argv[2] : "5800";

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        std::fprintf(stderr, "FAIL: getaddrinfo\n");
        return 1;
    }
    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        if (fd >= 0) { close(fd); fd = -1; }
    }
    freeaddrinfo(res);
    if (fd < 0) {
        std::fprintf(stderr, "FAIL: connect %s:%s\n", host, port);
        return 1;
    }

    remote_fb_hello hello{};
    if (remote_fb_recv_all(fd, &hello, sizeof(hello)) != 0) {
        std::fprintf(stderr, "FAIL: no HELLO\n");
        return 1;
    }
    if (hello.type != REMOTE_FB_MSG_HELLO || hello.magic != REMOTE_FB_MAGIC ||
        hello.version != REMOTE_FB_VERSION || hello.format != REMOTE_FB_FMT_RGB565) {
        std::fprintf(stderr, "FAIL: bad HELLO type=%u magic=%08x ver=%u fmt=%u\n",
                     hello.type, hello.magic, hello.version, hello.format);
        return 1;
    }
    const int W = hello.width, H = hello.height;
    std::printf("HELLO ok: %dx%d RGB565 v%d\n", W, H, hello.version);

    int frames = 0;
    while (frames < 3) {
        uint8_t type = 0;
        if (remote_fb_recv_all(fd, &type, 1) != 0) { std::fprintf(stderr, "FAIL: stream ended\n"); return 1; }
        if (type != REMOTE_FB_MSG_FRAME) { std::fprintf(stderr, "FAIL: unexpected msg %u\n", type); return 1; }
        remote_fb_frame f{};
        f.type = type;
        if (remote_fb_recv_all(fd, reinterpret_cast<uint8_t*>(&f) + 1, sizeof(f) - 1) != 0) {
            std::fprintf(stderr, "FAIL: short frame header\n"); return 1;
        }
        const size_t px = static_cast<size_t>(f.w) * f.h;
        std::vector<uint16_t> region(px);
        if (remote_fb_recv_all(fd, region.data(), px * 2) != 0) {
            std::fprintf(stderr, "FAIL: short frame payload\n"); return 1;
        }
        if (f.w != W || f.h != H) {
            std::fprintf(stderr, "FAIL: frame size %ux%u != %dx%d\n", f.w, f.h, W, H); return 1;
        }
        // top-left marker is rgb565(255,0,0) = 0xF800 in the mock pattern.
        if (region[0] != 0xF800) {
            std::fprintf(stderr, "FAIL: top-left pixel %04x != F800\n", region[0]); return 1;
        }
        ++frames;
    }
    std::printf("frames ok: %d full %dx%d frames, marker verified\n", frames, W, H);

    remote_fb_key k{};
    k.type = REMOTE_FB_MSG_KEY;
    k.key = '5';
    k.pressed = 1;
    if (remote_fb_send_all(fd, &k, sizeof(k)) != 0) {
        std::fprintf(stderr, "FAIL: key send\n"); return 1;
    }
    std::printf("key forwarded ok\n");

    close(fd);
    std::printf("PASS\n");
    return 0;
}
