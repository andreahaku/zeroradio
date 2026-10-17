/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "run_app.h"

#include "asset_manager.h"
#include "docs.h"
#include "i18n.h"
#include "linux_input.h"
#include "logger.h"
#include "remote_fb.h"
#include "text_viewer.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

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
    // driven with no physical display attached.
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

// H: the app's help page in a full-screen reader. Lives until closed or until
// run_app ends (then it is dropped before the display is released).
struct HelpState {
    ShellViewModel* shell;
    app::AssetManager* assets;
    std::unique_ptr<view::widgets::TextViewer> viewer;
};

void help_handler_trampoline(void* ctx) {
    auto* st = static_cast<HelpState*>(ctx);
    if (st->shell->help_doc().empty()) return;
    std::string text = read_doc(st->shell->help_doc());
    if (text.empty()) text = "The help page is not installed.";
    st->viewer = std::make_unique<view::widgets::TextViewer>(
        *st->assets, st->shell->is_dark_mode(),
        std::vector<view::widgets::TextViewer::Page>{{"Help", text, {}}});
}

void home_handler_trampoline(void* ctx) {
    static_cast<ShellViewModel*>(ctx)->request_home();
}

// Overlays drawn on the top layer: the ESC hold prompt and the key tooltip.
// Inter carries the accented letters of the translations; the built-in
// Montserrat fallback is ASCII only.
struct Overlays {
    ShellViewModel* shell{nullptr};
    lv_font_t* font{nullptr};
    lv_font_t* bold{nullptr};
    lv_obj_t* hold_box{nullptr};
    lv_obj_t* hint_box{nullptr};
    lv_timer_t* hint_timer{nullptr};
};
Overlays overlays;

constexpr uint32_t kHintShowMs = 4000;

lv_obj_t* make_box(lv_coord_t width) {
    auto* box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, width);
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1f3a5f), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_90, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x5b8fd0), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    return box;
}

lv_obj_t* add_label(lv_obj_t* box, const char* text, const lv_font_t* font) {
    auto* label = lv_label_create(box);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    return label;
}

void bar_anim_cb(void* bar, int32_t value) {
    lv_bar_set_value(static_cast<lv_obj_t*>(bar), value, LV_ANIM_OFF);
}

// ESC held past kEscHoldHintMs: a small modal with a bar that fills until the
// app leaves for the launcher at kEscHoldHomeMs (CardputerZero convention).
void hold_hint_trampoline(bool show, void* /*ctx*/) {
    if (!show) {
        if (overlays.hold_box) lv_obj_delete(overlays.hold_box); // also ends the bar animation
        overlays.hold_box = nullptr;
        return;
    }
    if (overlays.hold_box) return;
    overlays.hold_box = make_box(220);
    add_label(overlays.hold_box,
              i18n::pick({"Keep holding ESC to exit",
                          "Tieni premuto ESC per uscire",
                          "Maintenez ESC pour quitter",
                          "ESC gedrückt halten zum Beenden",
                          "Mantén pulsado ESC para salir"}),
              overlays.font);
    auto* bar = lv_bar_create(overlays.hold_box);
    lv_obj_set_size(bar, LV_PCT(100), 6);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0d1b2a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x5b8fd0), LV_PART_INDICATOR);
    lv_obj_align(overlays.hold_box, LV_ALIGN_CENTER, 0, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, bar);
    lv_anim_set_exec_cb(&anim, bar_anim_cb);
    lv_anim_set_values(&anim, 0, 100);
    lv_anim_set_duration(&anim, platform::kEscHoldHomeMs - platform::kEscHoldHintMs);
    lv_anim_start(&anim);
}

void close_nav_hint() {
    if (overlays.hint_timer) lv_timer_delete(overlays.hint_timer);
    overlays.hint_timer = nullptr;
    if (overlays.hint_box) lv_obj_delete(overlays.hint_box);
    overlays.hint_box = nullptr;
}

void hint_timer_cb(lv_timer_t* /*timer*/) {
    overlays.hint_timer = nullptr; // a one-shot timer deletes itself
    close_nav_hint();
}

// SYM + 4..8: what that NavBar key does on this page, in the user's language.
// Closes on any key or after kHintShowMs.
void nav_hint_trampoline(int slot, void* /*ctx*/) {
    close_nav_hint();
    if (!overlays.shell) return;
    const i18n::Text hint = overlays.shell->nav_hint(slot);
    const i18n::Text title_format{"Key %d", "Tasto %d", "Touche %d", "Taste %d", "Tecla %d"};
    char title[24];
    std::snprintf(title, sizeof(title), i18n::pick(title_format), slot + 4);

    overlays.hint_box = make_box(view::kScreenWidth - 24);
    add_label(overlays.hint_box, title, overlays.bold ? overlays.bold : overlays.font);
    add_label(overlays.hint_box,
              hint.empty() ? i18n::pick({"This key does nothing on this page.",
                                         "Questo tasto non fa nulla in questa pagina.",
                                         "Cette touche n’a pas d’effet sur cette page.",
                                         "Diese Taste hat auf dieser Seite keine Funktion.",
                                         "Esta tecla no hace nada en esta página."})
                           : i18n::pick(hint),
              overlays.font);
    lv_obj_align(overlays.hint_box, LV_ALIGN_BOTTOM_MID, 0, -(view::kNavBarHeight + 4));

    overlays.hint_timer = lv_timer_create(hint_timer_cb, kHintShowMs, nullptr);
    lv_timer_set_repeat_count(overlays.hint_timer, 1);
}

// Any key closes the tooltip; a new SYM + key opens the next one.
void key_activity_trampoline(uint32_t key, void* /*ctx*/) {
    if (key < platform::kKeyNavHint || key >= platform::kKeyNavHint + platform::kNavKeyCount) {
        close_nav_hint();
    }
}

void arrow_handler_trampoline(uint32_t key, void* ctx) {
    auto* shell = static_cast<ShellViewModel*>(ctx);
    switch (key) {
        case LV_KEY_UP:    shell->on_up(); break;
        case LV_KEY_DOWN:  shell->on_down(); break;
        case LV_KEY_LEFT:  shell->on_left(); break;
        case LV_KEY_RIGHT: shell->on_right(); break;
        default: break;
    }
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
    platform::set_home_handler(home_handler_trampoline, hold_hint_trampoline, &shell);
    overlays = Overlays{&shell, assets.load_font("inter-regular.ttf", 13),
                        assets.load_font("inter-semibold.ttf", 13), nullptr, nullptr, nullptr};
    platform::set_nav_hint_handler(nav_hint_trampoline, &shell);
    platform::set_key_activity_handler(key_activity_trampoline, &shell);
    HelpState help{&shell, &assets, nullptr};
    platform::set_help_handler(help_handler_trampoline, &help);

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
    platform::set_home_handler(nullptr, nullptr, nullptr);
    platform::set_nav_hint_handler(nullptr, nullptr);
    platform::set_key_activity_handler(nullptr, nullptr);
    hold_hint_trampoline(false, nullptr);
    close_nav_hint();
    overlays = Overlays{};
    platform::set_help_handler(nullptr, nullptr);
    help.viewer.reset();
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
    } else {
        // Stop the remote-fb server thread (no-op if unused): a joinable
        // std::thread left at exit aborts the process.
        platform::remote_fb_destroy();
    }

    return shell.exit_code();
}

} // namespace toolkit
