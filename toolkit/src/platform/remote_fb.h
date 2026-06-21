/*
 * SPDX-License-Identifier: MIT
 *
 * Headless "remote framebuffer" LVGL display (Path B): the app renders with no
 * physical screen and streams its framebuffer over TCP to a desktop viewer,
 * which forwards keystrokes back. Used to run a cardputer-radio app on a device
 * with no display attached yet (e.g. a Raspberry Pi Zero 2 W with the RTL-SDR)
 * and drive/observe it from the X1.
 *
 * Enabled at runtime by the REMOTE_FB env var (a port, or "1" for the default),
 * so the SAME binary (desktop or device) can switch to remote mode — see
 * run_app.cpp init_display().
 */
#ifndef TOOLKIT_PLATFORM_REMOTE_FB_H
#define TOOLKIT_PLATFORM_REMOTE_FB_H

#include "lvgl.h"

namespace platform {

// Create the headless streaming display (app = TCP server, binds `port`) and a
// keypad indev fed by keys the viewer forwards back. Returns nullptr on failure.
lv_display_t* remote_fb_create(int width, int height, int port);

} // namespace platform

#endif // TOOLKIT_PLATFORM_REMOTE_FB_H
