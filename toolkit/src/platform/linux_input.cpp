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
void (*key_capture)(uint32_t, void*) = nullptr;
void* capture_ctx = nullptr;

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

void key_event_cb(lv_event_t* event) {
    LV_UNUSED(event);

    auto* indev = lv_indev_active();
    if (!indev) {
        return;
    }

    const auto key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    if (pressed && (!last_key_pressed || last_key != key)) {
        dispatch_nav_key(key);
    }

    last_key = key;
    last_key_pressed = pressed;
}

#if !USE_DESKTOP
struct EvdevKeypad {
    int fd{-1};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
    uint32_t key{0};
};

uint32_t map_evdev_key(uint16_t code) {
    switch (code) {
        case KEY_ESC:        return LV_KEY_ESC;
        // Arrow keys + enter drive list navigation (the Radio hub menu and the
        // aircraft/node/ship lists): a portable selection scheme alongside the
        // 4-8 nav bar. Ignored by screens that don't consume them.
        case KEY_UP:         return LV_KEY_UP;
        case KEY_DOWN:       return LV_KEY_DOWN;
        case KEY_LEFT:       return LV_KEY_LEFT;
        case KEY_RIGHT:      return LV_KEY_RIGHT;
        // CardputerZero keyboard: the arrows live on the Fn layer of F/X/Z/C.
        // Mirror those physical keys (pressed WITHOUT Fn) onto the same nav so
        // the menu/lists can be driven either way. The apps are menu-driven and
        // never type these letters, so there's no conflict with text entry.
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
        case KEY_KPDOT:      return '.';
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

void set_quit_handler(void (*handler)(void* ctx), void* ctx) {
    quit_handler = handler;
    quit_ctx = ctx;
}

void set_key_capture(void (*handler)(uint32_t key, void* ctx), void* ctx) {
    key_capture = handler;
    capture_ctx = ctx;
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
