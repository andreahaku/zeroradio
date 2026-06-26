/*
 * SPDX-License-Identifier: MIT
 *
 * Headless remote-framebuffer LVGL display + key indev (Path B device side).
 * See remote_fb.h and tools/remote-fb/ for the viewer and the wire protocol.
 */
#include "remote_fb.h"

#include "linux_input.h"
#include "remote_fb_proto.h" // from tools/remote-fb/ (added to include dirs)

#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace platform {
namespace {

std::atomic<int> g_client{-1};       // connected viewer fd, or -1
std::atomic<bool> g_running{false};
std::atomic<bool> g_need_refresh{false};
int g_srv{-1};
std::thread g_server_thread;
lv_color_format_t g_cf{LV_COLOR_FORMAT_RGB565};
void* g_draw_buf{nullptr};
lv_display_t* g_disp{nullptr};

std::mutex g_key_mutex;
std::deque<std::pair<uint32_t, bool>> g_key_queue;
uint32_t g_last_key{0};

void send_region(const lv_area_t* area, uint8_t* px_map) {
    const int fd = g_client.load();
    if (std::getenv("REMOTE_FB_DEBUG")) {
        std::fprintf(stderr, "[remote-fb] flush area %d,%d %dx%d client=%d\n", area->x1, area->y1,
                     lv_area_get_width(area), lv_area_get_height(area), fd);
    }
    if (fd < 0) {
        return;
    }
    const int w = lv_area_get_width(area);
    const int h = lv_area_get_height(area);
    if (w <= 0 || h <= 0) {
        return;
    }

    std::vector<uint16_t> out(static_cast<size_t>(w) * h);
    if (g_cf == LV_COLOR_FORMAT_RGB565) {
        std::memcpy(out.data(), px_map, out.size() * 2);
    } else {
        // 32-bit render buffer: little-endian memory order is B, G, R, A.
        const uint8_t* p = px_map;
        for (size_t i = 0; i < out.size(); ++i) {
            const uint8_t b = p[0], g = p[1], r = p[2];
            p += 4;
            out[i] = static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }

    remote_fb_frame f{};
    f.type = REMOTE_FB_MSG_FRAME;
    f.x = static_cast<uint16_t>(area->x1);
    f.y = static_cast<uint16_t>(area->y1);
    f.w = static_cast<uint16_t>(w);
    f.h = static_cast<uint16_t>(h);
    if (remote_fb_send_all(fd, &f, sizeof(f)) != 0 ||
        remote_fb_send_all(fd, out.data(), out.size() * 2) != 0) {
        // Viewer gone: drop it. The server thread's recv() will also fail and
        // close the fd + go back to accept(); we just stop sending.
        g_client.store(-1);
    }
}

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    send_region(area, px_map);
    lv_display_flush_ready(disp);
}

void keypad_read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    std::lock_guard<std::mutex> lock(g_key_mutex);
    if (!g_key_queue.empty()) {
        const auto kv = g_key_queue.front();
        g_key_queue.pop_front();
        g_last_key = kv.first;
        data->key = kv.first;
        data->state = kv.second ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = !g_key_queue.empty();
    } else {
        data->key = g_last_key;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void refresh_timer_cb(lv_timer_t* /*timer*/) {
    if (g_need_refresh.exchange(false)) {
        lv_obj_t* screen = lv_screen_active();
        if (screen) {
            lv_obj_invalidate(screen); // mark the whole screen dirty
        }
        if (g_disp) {
            lv_refr_now(g_disp); // render + flush immediately (don't wait a cycle)
        }
    }
}

// Headless builds skip the SDL/driver init that normally registers LVGL's tick
// + delay source, so without these LVGL's clock never advances (timers freeze,
// only the initial frame renders). Provide a monotonic millisecond tick + a real
// sleep so lv_timer_handler/lv_delay_ms work with no display backend.
uint32_t tick_cb() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

void delay_cb(uint32_t ms) {
    usleep(ms * 1000);
}

void server_thread(int port, int w, int h) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        return;
    }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(srv, 1) != 0) {
        close(srv);
        return;
    }
    g_srv = srv;

    while (g_running.load()) {
        const int fd = accept(srv, nullptr, nullptr);
        if (fd < 0) {
            continue;
        }

        remote_fb_hello hello{};
        hello.type = REMOTE_FB_MSG_HELLO;
        hello.magic = REMOTE_FB_MAGIC;
        hello.version = REMOTE_FB_VERSION;
        hello.width = static_cast<uint16_t>(w);
        hello.height = static_cast<uint16_t>(h);
        hello.format = REMOTE_FB_FMT_RGB565;
        if (remote_fb_send_all(fd, &hello, sizeof(hello)) != 0) {
            close(fd);
            continue;
        }

        g_client.store(fd);
        g_need_refresh.store(true); // push a full frame to the fresh viewer

        // Read forwarded keys until the viewer disconnects.
        for (;;) {
            uint8_t type = 0;
            if (remote_fb_recv_all(fd, &type, 1) != 0) {
                break;
            }
            if (type == REMOTE_FB_MSG_KEY) {
                remote_fb_key k{};
                k.type = type;
                if (remote_fb_recv_all(fd, reinterpret_cast<uint8_t*>(&k) + 1, sizeof(k) - 1) != 0) {
                    break;
                }
                std::lock_guard<std::mutex> lock(g_key_mutex);
                g_key_queue.emplace_back(k.key, k.pressed != 0);
            }
        }

        g_client.store(-1);
        close(fd);
    }

    close(srv);
}

} // namespace

lv_display_t* remote_fb_create(int width, int height, int port) {
    lv_display_t* disp = lv_display_create(width, height);
    if (!disp) {
        return nullptr;
    }

    g_disp = disp;
    lv_tick_set_cb(tick_cb);   // headless: provide LVGL's clock...
    lv_delay_set_cb(delay_cb); // ...and a real sleep (no SDL/driver to do it)
    g_cf = lv_display_get_color_format(disp);
    const uint32_t bpp = lv_color_format_get_size(g_cf);
    const size_t buf_size = static_cast<size_t>(width) * height * bpp;
    g_draw_buf = std::malloc(buf_size);
    if (!g_draw_buf) {
        lv_display_delete(disp);
        return nullptr;
    }
    lv_display_set_buffers(disp, g_draw_buf, nullptr, static_cast<uint32_t>(buf_size),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    g_running.store(true);
    g_server_thread = std::thread(server_thread, port, width, height);

    lv_indev_t* keypad = lv_indev_create();
    lv_indev_set_type(keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keypad, keypad_read_cb);
    lv_indev_set_display(keypad, disp);
    attach_key_router(keypad);

    lv_timer_create(refresh_timer_cb, 100, nullptr);

    std::printf("remote-fb: serving %dx%d on TCP port %d (color_fmt=%d, %u bpp)\n",
                width, height, port, static_cast<int>(g_cf), bpp);
    std::fflush(stdout);
    return disp;
}

void remote_fb_destroy() {
    if (!g_running.exchange(false) && !g_server_thread.joinable() && !g_draw_buf) {
        return; // never started (or already torn down)
    }

    // Unblock the server thread: it may be parked in accept() on the listening
    // socket or in recv() on a connected client. shutdown() forces both to
    // return so the loop observes g_running == false and exits.
    const int client = g_client.exchange(-1);
    if (client >= 0) {
        ::shutdown(client, SHUT_RDWR);
        ::close(client);
    }
    if (g_srv >= 0) {
        ::shutdown(g_srv, SHUT_RDWR);
    }

    if (g_server_thread.joinable()) {
        g_server_thread.join();
    }
    g_srv = -1;

    std::free(g_draw_buf);
    g_draw_buf = nullptr;
    g_disp = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_key_mutex);
        g_key_queue.clear();
        g_last_key = 0;
    }
    g_need_refresh.store(false);
}

} // namespace platform
