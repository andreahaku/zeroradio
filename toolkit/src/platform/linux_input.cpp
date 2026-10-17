/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "linux_input.h"

#include <array>
#include <cstdint>
#include <cstring>

#if !USE_DESKTOP
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

namespace platform {
namespace {

std::array<lv_obj_t*, kNavKeyCount> nav_buttons{};
uint32_t last_key = 0;
bool last_key_pressed = false;
void (*quit_handler)(void*) = nullptr;
void* quit_ctx = nullptr;
void (*tab_handler)(void*) = nullptr;
void* tab_ctx = nullptr;
void (*arrow_handler)(uint32_t, void*) = nullptr;
void* arrow_ctx = nullptr;
void (*help_handler)(void*) = nullptr;
void* help_ctx = nullptr;
void (*home_handler)(void*) = nullptr;
void (*hold_hint_handler)(bool, void*) = nullptr;
void* home_ctx = nullptr;
void (*nav_hint_handler)(int, void*) = nullptr;
void* nav_hint_ctx = nullptr;
void (*key_activity_handler)(uint32_t, void*) = nullptr;
void* key_activity_ctx = nullptr;

void (*page_back_handler)(void*) = nullptr;
void* page_back_ctx = nullptr;

// Key 4 hold tracking (see set_page_back_handler).
lv_indev_t* page_indev = nullptr;
uint32_t page_started_at = 0;
bool page_back_sent = false;
lv_timer_t* page_timer = nullptr;

// ESC hold tracking (see set_home_handler).
lv_indev_t* esc_indev = nullptr;
uint32_t esc_started_at = 0;
bool esc_hint_shown = false;
bool esc_home_sent = false;
lv_timer_t* esc_timer = nullptr;
void (*key_capture)(uint32_t, void*) = nullptr;
void* capture_ctx = nullptr;
bool capture_text = false;

// SYM + 4..8 on the device, Shift + 4..8 on a desktop keyboard: the tooltip
// of that NavBar key. Returns -1 for any other key.
int nav_hint_slot(uint32_t key) {
    if (key >= kKeyNavHint && key < kKeyNavHint + kNavKeyCount) {
        return static_cast<int>(key - kKeyNavHint);
    }
    switch (key) {
        case '$': return 0;
        case '%': return 1;
        case '^': return 2;
        case '&': return 3;
        case '*': return 4;
        default:  return -1;
    }
}

size_t nav_key_to_index(uint32_t key) {
    switch (key) {
        case '4':
            return 0;
        case '5':
            return 1;
        case '6':
            return 2;
        case '7':
            return 3;
        case '8':
            return 4;
        default:
            return kNavKeyCount;
    }
}

void dispatch_nav_key(uint32_t key) {
    // A modal capture (e.g. the frequency dialog) gets every key raw and
    // bypasses all nav/quit routing: the physical 4-8 keys must type digits,
    // not trigger the toolbar buttons.
    if (key_capture) {
        key_capture(key, capture_ctx);
        return;
    }

    // ESC is dedicated to quit, independent of the nav buttons.
    if (key == LV_KEY_ESC) {
        if (quit_handler) {
            quit_handler(quit_ctx);
        }
        return;
    }
    if (key == LV_KEY_NEXT) {
        if (tab_handler) tab_handler(tab_ctx);
        return;
    }
    if (key == 'h' || key == 'H') {
        if (help_handler) help_handler(help_ctx);
        return;
    }
    if (key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
        if (arrow_handler) arrow_handler(key, arrow_ctx);
        return;
    }
    if (const int slot = nav_hint_slot(key); slot >= 0) {
        if (nav_hint_handler) nav_hint_handler(slot, nav_hint_ctx);
        return;
    }

    const auto index = nav_key_to_index(key);
    if (index >= nav_buttons.size()) {
        return;
    }

    auto* button = nav_buttons[index];
    if (!button || !lv_obj_is_valid(button) || !lv_obj_has_flag(button, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }

    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}

void esc_timer_cb(lv_timer_t* timer) {
    const bool held = esc_indev && lv_indev_get_state(esc_indev) == LV_INDEV_STATE_PRESSED &&
                      lv_indev_get_key(esc_indev) == LV_KEY_ESC;
    const uint32_t elapsed = lv_tick_elaps(esc_started_at);
    if (!held) {
        lv_timer_pause(timer);
        // A short press goes back. Released once the exit prompt is up, the
        // hold is cancelled and nothing happens.
        const bool short_press = !esc_hint_shown && !esc_home_sent;
        if (esc_hint_shown && hold_hint_handler) hold_hint_handler(false, home_ctx);
        esc_hint_shown = false;
        if (short_press && quit_handler) quit_handler(quit_ctx);
        esc_indev = nullptr;
        return;
    }
    if (esc_home_sent) return;
    if (elapsed >= kEscHoldHomeMs) {
        esc_home_sent = true;
        if (esc_hint_shown && hold_hint_handler) hold_hint_handler(false, home_ctx);
        esc_hint_shown = false;
        if (home_handler) home_handler(home_ctx);
        else if (quit_handler) quit_handler(quit_ctx);
    } else if (elapsed >= kEscHoldHintMs && !esc_hint_shown) {
        esc_hint_shown = true;
        if (hold_hint_handler) hold_hint_handler(true, home_ctx);
    }
}

void page_timer_cb(lv_timer_t* timer) {
    const bool held = page_indev && lv_indev_get_state(page_indev) == LV_INDEV_STATE_PRESSED &&
                      lv_indev_get_key(page_indev) == '4';
    if (!held) {
        lv_timer_pause(timer);
        if (!page_back_sent) dispatch_nav_key('4'); // a tap: the next page
        page_indev = nullptr;
        return;
    }
    if (!page_back_sent && lv_tick_elaps(page_started_at) >= kPageHoldMs) {
        page_back_sent = true;
        if (page_back_handler) page_back_handler(page_back_ctx);
    }
}

// Key 4 down with no capture: a tap goes to the next page on release, a hold
// goes back one page once kPageHoldMs has passed.
void start_page_hold(lv_indev_t* indev) {
    page_indev = indev;
    page_started_at = lv_tick_get();
    page_back_sent = false;
    if (!page_timer) page_timer = lv_timer_create(page_timer_cb, 40, nullptr);
    lv_timer_resume(page_timer);
}

// ESC down with no capture: decide short vs hold in esc_timer_cb.
void start_esc_hold(lv_indev_t* indev) {
    esc_indev = indev;
    esc_started_at = lv_tick_get();
    esc_hint_shown = false;
    esc_home_sent = false;
    if (!esc_timer) esc_timer = lv_timer_create(esc_timer_cb, 40, nullptr);
    lv_timer_resume(esc_timer);
}

void key_event_cb(lv_event_t* event) {
    LV_UNUSED(event);

    auto* indev = lv_indev_active();
    if (!indev) {
        return;
    }

    const auto key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    if (pressed && (!last_key_pressed || last_key != key)) {
        if (key_activity_handler) key_activity_handler(key, key_activity_ctx);
        if (key == LV_KEY_ESC && !key_capture && !esc_indev) {
            start_esc_hold(indev);
        } else if (key == '4' && !key_capture && page_back_handler) {
            if (!page_indev) start_page_hold(indev);
        } else if (key != LV_KEY_ESC || key_capture) {
            dispatch_nav_key(key);
        }
    }

    last_key = key;
    last_key_pressed = pressed;
}

#if !USE_DESKTOP
// The CardputerZero keyboard driver reports SYM as a key of its own, held
// while the symbol layer is active.
bool sym_held = false;

// SYM + 4..8: the symbol layer turns the five NavBar keys into these codes.
// Outside a key capture they open the key tooltips instead.
uint32_t map_sym_nav_key(uint16_t code) {
    switch (code) {
        case KEY_APOSTROPHE: return kKeyNavHint + 0; // SYM + 4
        case KEY_GRAVE:      return kKeyNavHint + 1; // SYM + 5
        case KEY_BACKSLASH:  return kKeyNavHint + 2; // SYM + 6
        case KEY_COMMA:      return kKeyNavHint + 3; // SYM + 7
        case KEY_DOT:        return kKeyNavHint + 4; // SYM + 8
        default:             return 0;
    }
}

struct EvdevKeypad {
    int fd{-1};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
    uint32_t key{0};
};

// Text entry for a modal capture (e.g. the location search): letters, space,
// comma and minus as characters. Returns 0 for anything else, which then goes
// through the normal nav mapping below.
uint32_t map_evdev_text(uint16_t code) {
    static constexpr struct { uint16_t code; char ch; } kText[] = {
        {KEY_A, 'a'}, {KEY_B, 'b'}, {KEY_C, 'c'}, {KEY_D, 'd'}, {KEY_E, 'e'}, {KEY_F, 'f'},
        {KEY_G, 'g'}, {KEY_H, 'h'}, {KEY_I, 'i'}, {KEY_J, 'j'}, {KEY_K, 'k'}, {KEY_L, 'l'},
        {KEY_M, 'm'}, {KEY_N, 'n'}, {KEY_O, 'o'}, {KEY_P, 'p'}, {KEY_Q, 'q'}, {KEY_R, 'r'},
        {KEY_S, 's'}, {KEY_T, 't'}, {KEY_U, 'u'}, {KEY_V, 'v'}, {KEY_W, 'w'}, {KEY_X, 'x'},
        {KEY_Y, 'y'}, {KEY_Z, 'z'}, {KEY_SPACE, ' '}, {KEY_COMMA, ','}, {KEY_MINUS, '-'},
    };
    for (const auto& t : kText) {
        if (t.code == code) return static_cast<uint32_t>(t.ch);
    }
    return 0;
}

uint32_t map_evdev_key(uint16_t code) {
    if (sym_held && !key_capture) {
        if (const uint32_t hint = map_sym_nav_key(code)) return hint;
    }
    // While a text-entry capture is active, F/X/Z/C are letters again (the Fn
    // layer still sends real arrows).
    if (capture_text) {
        if (const uint32_t ch = map_evdev_text(code)) return ch;
    }
    switch (code) {
        case KEY_ESC:        return LV_KEY_ESC;
        case KEY_TAB:        return LV_KEY_NEXT;
        case KEY_H:          return 'h'; // help page
        case KEY_HELP:       return 'h'; // Fn+H, the system's help key
        // Arrow keys + enter drive list navigation (the Radio hub menu and the
        // aircraft/node/ship lists): a portable selection scheme alongside the
        // 4-8 nav bar. Ignored by screens that don't consume them.
        case KEY_UP:         return LV_KEY_UP;
        case KEY_DOWN:       return LV_KEY_DOWN;
        case KEY_LEFT:       return LV_KEY_LEFT;
        case KEY_RIGHT:      return LV_KEY_RIGHT;
        // CardputerZero keyboard: the arrows live on the Fn layer of F/X/Z/C.
        // Mirror those physical keys (pressed WITHOUT Fn) onto the same nav so
        // the menu/lists can be driven either way. A text-entry capture takes
        // them as letters instead, see map_evdev_text().
        case KEY_F:          return LV_KEY_UP;
        case KEY_X:          return LV_KEY_DOWN;
        case KEY_Z:          return LV_KEY_LEFT;
        case KEY_C:          return LV_KEY_RIGHT;
        // Digits, dot, backspace and enter feed the frequency dialog (and the
        // nav routing for 4-8). The rest are ignored outside the dialog.
        case KEY_0:          return '0';
        case KEY_1:          return '1';
        case KEY_2:          return '2';
        case KEY_3:          return '3';
        case KEY_4:          return '4';
        case KEY_5:          return '5';
        case KEY_6:          return '6';
        case KEY_7:          return '7';
        case KEY_8:          return '8';
        case KEY_9:          return '9';
        case KEY_DOT:
        case KEY_KPDOT:
        // The CardputerZero driver sends its "." key (on the symbol layer) as
        // KEY_HIRAGANA.
        case KEY_HIRAGANA:   return '.';
        case KEY_BACKSPACE:  return LV_KEY_BACKSPACE;
        case KEY_ENTER:
        case KEY_KPENTER:    return LV_KEY_ENTER;
        default:             return 0;
    }
}

bool has_nav_keys(int fd) {
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    auto has_key = [&](int code) {
        const auto bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
        return (key_bits[code / bits_per_word] & (1UL << (code % bits_per_word))) != 0;
    };

    return has_key(KEY_ESC) || has_key(KEY_4) || has_key(KEY_5) || has_key(KEY_6) ||
           has_key(KEY_7) || has_key(KEY_8);
}

void evdev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (!keypad) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    input_event input{};
    while (read(keypad->fd, &input, sizeof(input)) == sizeof(input)) {
        if (input.type != EV_KEY) {
            continue;
        }
        if (input.code == KEY_CD) { // SYM, see sym_held
            sym_held = input.value != 0;
            continue;
        }

        const auto key = map_evdev_key(input.code);
        if (!key || input.value == 2) {
            continue;
        }

        keypad->key = key;
        keypad->state = input.value ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = true;
        break;
    }

    data->key = keypad->key;
    data->state = keypad->state;
}

void evdev_delete_cb(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (!keypad) {
        return;
    }

    if (keypad->fd >= 0) {
        close(keypad->fd);
    }
    delete keypad;
}

lv_indev_t* create_keypad_from_fd(int fd) {
    auto* keypad = new EvdevKeypad;
    keypad->fd = fd;

    auto* indev = lv_indev_create();
    if (!indev) {
        delete keypad;
        close(fd);
        return nullptr;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, evdev_read_cb);
    lv_indev_set_driver_data(indev, keypad);
    lv_indev_add_event_cb(indev, evdev_delete_cb, LV_EVENT_DELETE, nullptr);
    attach_key_router(indev);
    return indev;
}

lv_indev_t* try_create_keypad(const char* path) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }

    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LV_LOG_WARN("failed to open input device %s: %s", path, strerror(errno));
        return nullptr;
    }

    if (!has_nav_keys(fd)) {
        close(fd);
        return nullptr;
    }

    LV_LOG_INFO("using evdev key input %s", path);
    return create_keypad_from_fd(fd);
}

void discover_keypads(lv_display_t* display) {
    const char* configured_device = APP_KEY_INPUT_DEVICE;
    if (configured_device[0] != '\0') {
        auto* indev = try_create_keypad(configured_device);
        if (indev) {
            lv_indev_set_display(indev, display);
        }
        return;
    }

    auto* dir = opendir("/dev/input");
    if (!dir) {
        LV_LOG_WARN("failed to open /dev/input: %s", strerror(errno));
        return;
    }

    while (auto* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        std::string path = "/dev/input/";
        path += entry->d_name;
        auto* indev = try_create_keypad(path.c_str());
        if (indev) {
            lv_indev_set_display(indev, display);
        }
    }

    closedir(dir);
}
#endif

} // namespace

void init_key_input(lv_display_t* display) {
#if !USE_DESKTOP
    discover_keypads(display);
#else
    LV_UNUSED(display);
#endif
}

void attach_key_router(lv_indev_t* indev) {
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) {
        return;
    }

    lv_indev_add_event_cb(indev, key_event_cb, LV_EVENT_KEY, nullptr);
}

void set_home_handler(void (*home)(void* ctx), void (*hint)(bool show, void* ctx), void* ctx) {
    home_handler = home;
    hold_hint_handler = hint;
    home_ctx = ctx;
}

void set_help_handler(void (*handler)(void* ctx), void* ctx) {
    help_handler = handler;
    help_ctx = ctx;
}

void set_arrow_handler(void (*handler)(uint32_t key, void* ctx), void* ctx) {
    arrow_handler = handler;
    arrow_ctx = ctx;
}

void set_page_back_handler(void (*handler)(void* ctx), void* ctx) {
    page_back_handler = handler;
    page_back_ctx = ctx;
}

void set_nav_hint_handler(void (*handler)(int slot, void* ctx), void* ctx) {
    nav_hint_handler = handler;
    nav_hint_ctx = ctx;
}

void set_key_activity_handler(void (*handler)(uint32_t key, void* ctx), void* ctx) {
    key_activity_handler = handler;
    key_activity_ctx = ctx;
}

void set_tab_handler(void (*handler)(void* ctx), void* ctx) {
    tab_handler = handler;
    tab_ctx = ctx;
}

void set_quit_handler(void (*handler)(void* ctx), void* ctx) {
    quit_handler = handler;
    quit_ctx = ctx;
}

void set_key_capture(void (*handler)(uint32_t key, void* ctx), void* ctx, bool text) {
    key_capture = handler;
    capture_ctx = ctx;
    capture_text = handler != nullptr && text;
}

void register_nav_button(size_t index, lv_obj_t* button) {
    if (index >= nav_buttons.size()) {
        return;
    }

    nav_buttons[index] = button;
}

void unregister_nav_button(size_t index, lv_obj_t* button) {
    if (index >= nav_buttons.size()) {
        return;
    }

    if (!button || nav_buttons[index] == button) {
        nav_buttons[index] = nullptr;
    }
}

} // namespace platform
