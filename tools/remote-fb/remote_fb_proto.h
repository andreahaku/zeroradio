/*
 * SPDX-License-Identifier: MIT
 *
 * remote-fb wire protocol (Path B): the app runs on the device (e.g. a Raspberry
 * Pi Zero 2 W with the RTL-SDR attached) and renders headless; it streams its
 * 320x170 framebuffer over TCP to a standalone viewer on the desktop, which also
 * forwards key presses back so the remote app is drivable.
 *
 * Self-contained header (no LVGL, no SDL): included by BOTH the device-side
 * driver (toolkit/src/platform/remote_fb.cpp) and the desktop viewer/mock.
 *
 * Roles: the APP is the SERVER (binds + listens); the VIEWER is the CLIENT
 * (connects). The viewer can reconnect at any time; on each new connection the
 * app sends HELLO followed by one full frame.
 *
 * Byte order: both ends are little-endian (x86-64 desktop, aarch64 Pi), so
 * multi-byte fields are sent in host order. HELLO carries a magic+version so a
 * mismatch is detected rather than silently mis-rendered.
 */
#ifndef REMOTE_FB_PROTO_H
#define REMOTE_FB_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif

#if defined(_WIN32)
#error "remote-fb is POSIX-only (Linux desktop + device)"
#endif

#include <sys/socket.h>
#include <sys/types.h>

#define REMOTE_FB_MAGIC 0x52464231u /* "RFB1" */
#define REMOTE_FB_VERSION 1
#define REMOTE_FB_DEFAULT_PORT 5800

/* Wire pixel format. The CardputerZero / the SPI panel are RGB565, and the
 * device LVGL build renders 16-bit, so RGB565 is the native, zero-copy case. */
enum remote_fb_format {
    REMOTE_FB_FMT_RGB565 = 1,
};

/* Message types (1 byte tag, then a fixed header, then optional payload). */
enum remote_fb_msg {
    REMOTE_FB_MSG_HELLO = 0x01, /* app -> viewer, once per connection */
    REMOTE_FB_MSG_FRAME = 0x02, /* app -> viewer, one per flushed region */
    REMOTE_FB_MSG_KEY   = 0x03, /* viewer -> app, a key down/up event   */
};

#pragma pack(push, 1)
typedef struct {
    uint8_t  type;    /* REMOTE_FB_MSG_HELLO */
    uint32_t magic;   /* REMOTE_FB_MAGIC */
    uint16_t version; /* REMOTE_FB_VERSION */
    uint16_t width;   /* full screen width  (e.g. 320) */
    uint16_t height;  /* full screen height (e.g. 170) */
    uint8_t  format;  /* remote_fb_format */
    uint8_t  reserved;
} remote_fb_hello;

typedef struct {
    uint8_t  type;    /* REMOTE_FB_MSG_FRAME */
    uint16_t x;       /* region top-left x */
    uint16_t y;       /* region top-left y */
    uint16_t w;       /* region width  */
    uint16_t h;       /* region height */
    /* followed by w*h*2 bytes of RGB565, row-major, no padding */
} remote_fb_frame;

typedef struct {
    uint8_t  type;    /* REMOTE_FB_MSG_KEY */
    uint32_t key;     /* LVGL key code: '0'-'9', '.', LV_KEY_ESC(27),
                         LV_KEY_ENTER(10), LV_KEY_BACKSPACE(8), nav '4'-'8' */
    uint8_t  pressed; /* 1 = down, 0 = up */
} remote_fb_key;
#pragma pack(pop)

/* Blocking I/O helpers shared by both ends. Return 0 on success, -1 on
 * error/EOF. send_all/recv_all loop over short reads/writes. */
static inline int remote_fb_send_all(int fd, const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static inline int remote_fb_recv_all(int fd, void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

#endif /* REMOTE_FB_PROTO_H */
