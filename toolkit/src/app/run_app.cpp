/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "run_app.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "logger.h"
#include "remote_fb.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdlib>

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

namespace toolkit {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

lv_display_t* init_display() {
    // Path B: if REMOTE_FB is set (a port, or "1" for the default 5800), run
    // headless and stream the framebuffer to a desktop viewer over TCP, with key
    // forwarding back. Works in ANY build (desktop or device), so the app can be
    // driven with no physical display attached (e.g. a Pi Zero 2 W + RTL-SDR).
    if (const char* env = std::getenv("REMOTE_FB")) {
        int port = std::atoi(env);
        if (port <= 0) {
            port = 5800; // REMOTE_FB_DEFAULT_PORT
        }
        return platform::remote_fb_create(view::kScreenWidth, view::kScreenHeight, port);
    }
#if USE_DESKTOP
    auto* display = lv_sdl_window_create(view::kScreenWidth, view::kScreenHeight);
    if (!display) {
        return nullptr;
    }

    lv_sdl_window_set_title(display, "ZeroRadio");
    lv_sdl_window_set_resizeable(display, false);
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    auto* keyboard = lv_sdl_keyboard_create();
    platform::attach_key_router(keyboard);
    return display;
#elif APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    // LV_LINUX_FBDEV_DEVICE (set by the launcher/platform) wins over the build default.
    const char* fbdev = std::getenv("LV_LINUX_FBDEV_DEVICE");
    if (!fbdev || fbdev[0] == '\0') fbdev = APP_FRAMEBUFFER_DEVICE;
    if (lv_linux_fbdev_set_file(display, fbdev) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}

void quit_handler_trampoline(void* ctx) {
    static_cast<ShellViewModel*>(ctx)->request_quit();
}

void tab_handler_trampoline(void* ctx) {
    static_cast<ShellViewModel*>(ctx)->on_tab();
}

void arrow_handler_trampoline(int dir, void* ctx) {
    auto* shell = static_cast<ShellViewModel*>(ctx);
    if (dir < 0) shell->on_up();
    else shell->on_down();
}

} // namespace

int run_app(ShellViewModel& shell,
            app::AssetManager& assets,
            const std::function<lv_obj_t*()>& build_root,
            const std::function<void()>& on_teardown) {
    logger::Logger::init();
    logger::Logger::set_tag("zeroradio");

    lv_init();

    auto* display = init_display();
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    view::apply_lvgl_theme(display, shell.is_dark_mode());

    // ESC quits from any tool page (decoupled from nav key '4').
    platform::set_quit_handler(quit_handler_trampoline, &shell);
    platform::set_tab_handler(tab_handler_trampoline, &shell);
    platform::set_arrow_handler(arrow_handler_trampoline, &shell);

    // PLUGIN HOOK: the UI is constructed here independently of how the display
    // was created above. For the CardputerZero emulator (which dlopen()s a
    // shared object and calls `ui_init()` against a display it owns), extract
    // the build_root() call below into `extern "C" void ui_init() { ... }` and
    // skip init_display()/the run loop. The shell + screen graph is
    // display-agnostic, so no UI code needs to change for the plugin path.
    lv_obj_t* screen_root = build_root ? build_root() : nullptr;
    if (!screen_root) {
        LOG_ERROR("build_root() returned no screen");
        return 1;
    }
    lv_screen_load(screen_root);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(shell.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("LVGL app started at {}x{}", lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    while (running) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }

    // Release global input handlers that point at the soon-to-be-destroyed
    // shell / screen (matters if run_app() is ever re-entered, e.g. as a plugin).
    platform::set_quit_handler(nullptr, nullptr);
    platform::set_tab_handler(nullptr, nullptr);
    platform::set_arrow_handler(nullptr, nullptr);
    platform::set_key_capture(nullptr, nullptr);

    // Re-entrant teardown (the Radio hub): give the display back so a spawned
    // child can claim the single framebuffer/port. The caller deletes its screen
    // graph in on_teardown() — while the display is still alive — then we drop
    // every indev, stop the remote-fb server (no-op if unused) and delete the
    // display. lv_deinit() is intentionally left to the host (after its
    // AssetManager has freed its freetype fonts).
    if (on_teardown) {
        on_teardown();

        lv_indev_t* indev = lv_indev_get_next(nullptr);
        while (indev) {
            lv_indev_t* next = lv_indev_get_next(indev);
            lv_indev_delete(indev);
            indev = next;
        }

        platform::remote_fb_destroy();
        lv_display_delete(display);
    }

    return 0;
}

} // namespace toolkit
