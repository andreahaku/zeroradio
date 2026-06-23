/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
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
int run_app(ShellViewModel& shell,
            app::AssetManager& assets,
            const std::function<lv_obj_t*()>& build_root);

} // namespace toolkit
