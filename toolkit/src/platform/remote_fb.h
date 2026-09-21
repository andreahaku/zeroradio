/*
 * SPDX-License-Identifier: MIT
 *
 * Headless "remote framebuffer" LVGL display (Path B): the app renders with no
 * physical screen and streams its framebuffer over TCP to a desktop viewer,
 * which forwards keystrokes back. Used to run a cardputer-radio app on a device
 * with no display attached, and drive/observe it from a desktop viewer.
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

// Tear the streaming server down: stop the accept/serve thread, close the
// listening + client sockets (freeing the TCP port) and release the draw
// buffer. Idempotent and a no-op if remote-fb was never started. Required so a
// re-entrant host (the Radio hub) can hand the single display off to a spawned
// child and reclaim it afterwards without leaking the port. Call before
// lv_display_delete()/lv_deinit().
void remote_fb_destroy();

} // namespace platform

#endif // TOOLKIT_PLATFORM_REMOTE_FB_H
