/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "shell_viewmodel.h"

#include "lvgl.h"

#include <functional>

namespace app {
class AssetManager;
}

namespace toolkit {

// Generic app entry point: init LVGL, create the display (SDL on desktop /
// fbdev / DRM on device), apply the theme from the shell's dark-mode state, wire
// ESC -> shell.request_quit, then call build_root() to obtain the screen root,
// load it, and run the LVGL loop until shell.quit_requested is set. Returns the
// process exit code.
//
// `on_teardown` (optional) lets a host hand the lone display off to a spawned
// child and reclaim it (the Radio hub). When set, after the loop run_app calls
// on_teardown() — the caller MUST delete its screen graph there, while the
// display is still alive — then releases the session: deletes every indev, tears
// the remote-fb server down (freeing its TCP port) and deletes the display, so
// the framebuffer/port is free for the child while the host blocks in waitpid().
// It does NOT call lv_deinit(): LVGL + the SDL backend are not cleanly
// re-initialisable in-process, so the hub execs a fresh image to show the menu
// again rather than re-entering run_app. Legacy single-shot apps pass no
// callback and keep the original behaviour (the display lives until the process
// exits).
int run_app(ShellViewModel& shell,
            app::AssetManager& assets,
            const std::function<lv_obj_t*()>& build_root,
            const std::function<void()>& on_teardown = {});

} // namespace toolkit
