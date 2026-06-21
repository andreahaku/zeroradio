/*
 * SPDX-License-Identifier: MIT
 *
 * remote-fb viewer (desktop, X1): connects to a cardputer-radio app running
 * headless on a remote device (Path B), shows its 320x170 framebuffer in a
 * scaled SDL2 window, and forwards key presses back so the remote app is
 * drivable from here.
 *
 *   remote-fb-viewer <host> [port] [scale]
 *
 * Keys forwarded (mapped to the same LVGL codes the device key router expects):
 *   4 5 6 7 8 . 0-9   -> nav / freq dialog
 *   Esc               -> LV_KEY_ESC (quit / cancel)
 *   Enter             -> LV_KEY_ENTER
 *   Backspace         -> LV_KEY_BACKSPACE
 * The viewer window's own Esc does NOT close the viewer (it is forwarded);
 * close the window or Ctrl+C to quit the viewer.
 */
#include "remote_fb_proto.h"

#include <SDL2/SDL.h>

#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <netdb.h>
#include <thread>
#include <unistd.h>
#include <vector>

/* LVGL key codes we forward (kept in sync with lv_keyboard / linux_input.cpp). */
static constexpr uint32_t kLvEsc = 27;
static constexpr uint32_t kLvEnter = 10;
static constexpr uint32_t kLvBackspace = 8;

namespace {

int connect_to(const char* host, const char* port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        return -1;
    }
    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

uint32_t map_sdl_key(const SDL_Keysym& k) {
    switch (k.sym) {
        case SDLK_ESCAPE:    return kLvEsc;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  return kLvEnter;
        case SDLK_BACKSPACE: return kLvBackspace;
        case SDLK_PERIOD:
        case SDLK_KP_PERIOD: return '.';
        case SDLK_0: case SDLK_KP_0: return '0';
        case SDLK_1: case SDLK_KP_1: return '1';
        case SDLK_2: case SDLK_KP_2: return '2';
        case SDLK_3: case SDLK_KP_3: return '3';
        case SDLK_4: case SDLK_KP_4: return '4';
        case SDLK_5: case SDLK_KP_5: return '5';
        case SDLK_6: case SDLK_KP_6: return '6';
        case SDLK_7: case SDLK_KP_7: return '7';
        case SDLK_8: case SDLK_KP_8: return '8';
        case SDLK_9: case SDLK_KP_9: return '9';
        default:             return 0;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <host> [port=%d] [scale=3]\n", argv[0],
                     REMOTE_FB_DEFAULT_PORT);
        return 2;
    }
    const char* host = argv[1];
    const char* port = (argc >= 3) ? argv[2] : nullptr;
    char port_buf[16];
    if (!port) {
        std::snprintf(port_buf, sizeof(port_buf), "%d", REMOTE_FB_DEFAULT_PORT);
        port = port_buf;
    }
    const int scale = (argc >= 4) ? std::atoi(argv[3]) : 1; // 1:1 with the real 320x170 panel

    const int fd = connect_to(host, port);
    if (fd < 0) {
        std::fprintf(stderr, "remote-fb: cannot connect to %s:%s\n", host, port);
        return 1;
    }

    remote_fb_hello hello{};
    if (remote_fb_recv_all(fd, &hello, sizeof(hello)) != 0 ||
        hello.type != REMOTE_FB_MSG_HELLO || hello.magic != REMOTE_FB_MAGIC) {
        std::fprintf(stderr, "remote-fb: bad/absent HELLO (magic=%08x)\n", hello.magic);
        close(fd);
        return 1;
    }
    const int W = hello.width;
    const int H = hello.height;
    std::printf("remote-fb: connected to %s:%s  %dx%d fmt=%d v%d\n", host, port, W, H,
                hello.format, hello.version);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        close(fd);
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("cardputer-radio (remote-fb)",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       W * scale, H * scale, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                                         SDL_TEXTUREACCESS_STREAMING, W, H);

    // CPU-side framebuffer the network thread writes into; the main thread
    // uploads it to the GPU texture (SDL rendering stays on the main thread).
    std::vector<uint16_t> fb(static_cast<size_t>(W) * H, 0);
    std::mutex fb_mutex;
    std::atomic<bool> running{true};
    std::atomic<bool> dirty{true};

    std::thread reader([&]() {
        while (running.load()) {
            uint8_t type = 0;
            if (remote_fb_recv_all(fd, &type, 1) != 0) {
                break;
            }
            if (type == REMOTE_FB_MSG_FRAME) {
                remote_fb_frame f{};
                f.type = type;
                if (remote_fb_recv_all(fd, reinterpret_cast<uint8_t*>(&f) + 1,
                                       sizeof(f) - 1) != 0) {
                    break;
                }
                const size_t px = static_cast<size_t>(f.w) * f.h;
                std::vector<uint16_t> region(px);
                if (px && remote_fb_recv_all(fd, region.data(), px * 2) != 0) {
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(fb_mutex);
                    for (int row = 0; row < f.h; ++row) {
                        const int dst_y = f.y + row;
                        if (dst_y < 0 || dst_y >= H) continue;
                        const int copy_w = (f.x + f.w <= W) ? f.w : (W - f.x);
                        if (f.x < 0 || copy_w <= 0) continue;
                        std::memcpy(&fb[static_cast<size_t>(dst_y) * W + f.x],
                                    &region[static_cast<size_t>(row) * f.w],
                                    static_cast<size_t>(copy_w) * 2);
                    }
                }
                dirty.store(true);
            } else {
                // Unknown/other message: we only expect FRAME from the app.
                break;
            }
        }
        running.store(false);
    });

    while (running.load()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running.store(false);
            } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                const uint32_t key = map_sdl_key(ev.key.keysym);
                if (key && ev.key.repeat == 0) {
                    remote_fb_key k{};
                    k.type = REMOTE_FB_MSG_KEY;
                    k.key = key;
                    k.pressed = (ev.type == SDL_KEYDOWN) ? 1 : 0;
                    if (remote_fb_send_all(fd, &k, sizeof(k)) != 0) {
                        running.store(false);
                    }
                }
            }
        }

        if (dirty.exchange(false)) {
            std::lock_guard<std::mutex> lock(fb_mutex);
            SDL_UpdateTexture(tex, nullptr, fb.data(), W * 2);
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        SDL_Delay(8);
    }

    running.store(false);
    shutdown(fd, SHUT_RDWR);
    if (reader.joinable()) {
        reader.join();
    }
    close(fd);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
