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

void page_back_trampoline(void* ctx) {
    static_cast<ShellViewModel*>(ctx)->cycle_toolbar_back();
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
    lv_obj_t* toast_box{nullptr};
    lv_timer_t* toast_timer{nullptr};
};
Overlays overlays;

constexpr uint32_t kHintShowMs = 4000;
constexpr uint32_t kToastShowMs = 1200;

// System fonts with the Japanese and Chinese glyphs Inter lacks, in order of
// preference. LVGL opens face 0 of a collection, which in Noto Sans CJK is the
// Japanese one, so Chinese prefers Droid Sans Fallback (fonts-droid-fallback,
// a dependency of the package). Debian paths first, then Arch for the desktop.
constexpr const char* kJapaneseFonts[] = {
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
};
constexpr const char* kChineseFonts[] = {
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
};

// Japanese or Chinese: Inter keeps drawing the Latin text and the first CJK
// font found supplies the other glyphs, as an LVGL fallback font.
void attach_cjk_fallback(app::AssetManager& assets, uint32_t size) {
    const i18n::Lang lang = i18n::current();
    if (!i18n::needs_cjk_font(lang)) return;
    lv_font_t* cjk = nullptr;
    for (const char* path : lang == i18n::Lang::Ja ? kJapaneseFonts : kChineseFonts) {
        if ((cjk = assets.load_font(path, size))) break;
    }
    if (!cjk) {
        LOG_WARN("no CJK font found: the tooltips show missing glyphs");
        return;
    }
    if (overlays.font) overlays.font->fallback = cjk;
    else overlays.font = cjk;
    if (overlays.bold) overlays.bold->fallback = cjk;
}

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

// ESC held past kEscHoldHintMs: the ZeroTracker exit toast, "Hold ESC 3 s to
// exit" with ESC in the warning colour, until the app leaves for the launcher
// at kEscHoldHomeMs or the key is released.
void hold_hint_trampoline(bool show, void* /*ctx*/) {
    if (!show) {
        if (overlays.hold_box) lv_obj_delete(overlays.hold_box);
        overlays.hold_box = nullptr;
        return;
    }
    if (overlays.hold_box || !overlays.shell) return;
    const bool dark = overlays.shell->is_dark_mode();
    const auto colors = view::palette(dark);

    auto* box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(box, 96, 0);
    lv_obj_set_style_min_height(box, 36, 0);
    lv_obj_set_style_pad_hor(box, 14, 0);
    lv_obj_set_style_pad_ver(box, 8, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_bg_color(box, colors.button, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_90, 0);
    lv_obj_set_style_shadow_width(box, 8, 0);
    lv_obj_set_style_shadow_opa(box, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(box, lv_color_black(), 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);

    // "%s" is the key name, drawn in the warning colour through recolouring.
    const char* format = i18n::pick({"Hold %s 3 s to exit",
                                     "Tieni premuto %s 3 s per uscire",
                                     "Maintenez %s 3 s pour quitter",
                                     "%s 3 s halten zum Beenden",
                                     "Mantén %s 3 s para salir",
                                     "%s を3秒押し続けると終了",
                                     "按住 %s 3 秒退出"});
    char key[24];
    std::snprintf(key, sizeof(key), "#%06x ESC#", dark ? 0xf2c97du : 0xf0a020u);
    char text[96];
    std::snprintf(text, sizeof(text), format, key);

    auto* label = lv_label_create(box);
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, overlays.font ? overlays.font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, colors.text, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -38);
    overlays.hold_box = box;
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
    const i18n::Text title_format{"Key %d", "Tasto %d", "Touche %d", "Taste %d", "Tecla %d", "キー %d", "按键 %d"};
    char title[24];
    std::snprintf(title, sizeof(title), i18n::pick(title_format), slot + 4);

    overlays.hint_box = make_box(view::kScreenWidth - 24);
    add_label(overlays.hint_box, title, overlays.bold ? overlays.bold : overlays.font);
    add_label(overlays.hint_box,
              hint.empty() ? i18n::pick({"This key does nothing on this page.",
                                         "Questo tasto non fa nulla in questa pagina.",
                                         "Cette touche n’a pas d’effet sur cette page.",
                                         "Diese Taste hat auf dieser Seite keine Funktion.",
                                         "Esta tecla no hace nada en esta página.",
                                         "このページではこのキーは使いません。", "此键在本页面无功能。"})
                           : i18n::pick(hint),
              overlays.font);
    lv_obj_align(overlays.hint_box, LV_ALIGN_CENTER, 0, 0);

    overlays.hint_timer = lv_timer_create(hint_timer_cb, kHintShowMs, nullptr);
    lv_timer_set_repeat_count(overlays.hint_timer, 1);
}

// Any key closes the tooltip; a new SYM + key opens the next one.
void key_activity_trampoline(uint32_t key, void* /*ctx*/) {
    if (key < platform::kKeyNavHint || key >= platform::kKeyNavHint + platform::kNavKeyCount) {
        close_nav_hint();
    }
}

void close_toast() {
    if (overlays.toast_timer) lv_timer_delete(overlays.toast_timer);
    overlays.toast_timer = nullptr;
    if (overlays.toast_box) lv_obj_delete(overlays.toast_box);
    overlays.toast_box = nullptr;
}

void toast_timer_cb(lv_timer_t* /*timer*/) {
    overlays.toast_timer = nullptr; // a one-shot timer deletes itself
    close_toast();
}

// Fn + A/S/D: the app changes its volume and the new state shows for a moment.
void volume_trampoline(int step, void* ctx) {
    const std::string status = static_cast<ShellViewModel*>(ctx)->on_volume(step);
    if (status.empty()) return;
    close_toast();
    overlays.toast_box = make_box(LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(overlays.toast_box, 12, 0);
    auto* label = add_label(overlays.toast_box, status.c_str(), overlays.bold);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_align(overlays.toast_box, LV_ALIGN_CENTER, 0, 0);
    overlays.toast_timer = lv_timer_create(toast_timer_cb, kToastShowMs, nullptr);
    lv_timer_set_repeat_count(overlays.toast_timer, 1);
}

void arrow_handler_trampoline(uint32_t key, void* ctx) {
    auto* shell = static_cast<ShellViewModel*>(ctx);
    switch (key) {
        case LV_KEY_UP:    shell->on_up(); break;
        case LV_KEY_DOWN:  shell->on_down(); break;
        case LV_KEY_LEFT:  shell->on_left(); break;
        case LV_KEY_RIGHT: shell->on_right(); break;
        case LV_KEY_ENTER: shell->on_enter(); break;
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
    platform::set_page_back_handler(page_back_trampoline, &shell);
    platform::set_arrow_handler(arrow_handler_trampoline, &shell);
    platform::set_home_handler(home_handler_trampoline, hold_hint_trampoline, &shell);
    overlays = Overlays{};
    overlays.shell = &shell;
    overlays.font = assets.load_font("inter-regular.ttf", 13);
    overlays.bold = assets.load_font("inter-semibold.ttf", 13);
    attach_cjk_fallback(assets, 13);
    platform::set_nav_hint_handler(nav_hint_trampoline, &shell);
    platform::set_key_activity_handler(key_activity_trampoline, &shell);
    platform::set_volume_handler(volume_trampoline, &shell);
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
    platform::set_page_back_handler(nullptr, nullptr);
    platform::set_arrow_handler(nullptr, nullptr);
    platform::set_home_handler(nullptr, nullptr, nullptr);
    platform::set_nav_hint_handler(nullptr, nullptr);
    platform::set_key_activity_handler(nullptr, nullptr);
    platform::set_volume_handler(nullptr, nullptr);
    close_toast();
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
