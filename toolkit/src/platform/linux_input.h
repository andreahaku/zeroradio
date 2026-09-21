/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstddef>

namespace platform {

constexpr size_t kNavKeyCount = 5;

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);
void register_nav_button(size_t index, lv_obj_t* button);
void unregister_nav_button(size_t index, lv_obj_t* button);

// ESC is decoupled from the nav keys and routed here (e.g. to quit). The '4'
// key still maps to nav button 0; ESC no longer does.
void set_quit_handler(void (*handler)(void* ctx), void* ctx);

// TAB (LV_KEY_NEXT) outside a key capture: app switch (SDR <-> Survey).
void set_tab_handler(void (*handler)(void* ctx), void* ctx);

// Up/down (arrows, or F/X on the CardputerZero keyboard) outside a key
// capture: move the cursor of the visible list. `dir` is -1 (up) or +1 (down).
void set_arrow_handler(void (*handler)(int dir, void* ctx), void* ctx);

// H outside a key capture: open the app's help page.
void set_help_handler(void (*handler)(void* ctx), void* ctx);

// ESC outside a key capture follows the CardputerZero convention: a short press
// is the normal quit/back (set_quit_handler), holding it 3 s returns to the
// system launcher (`home`). `hint(true)` fires once the hold passes 0.5 s so the
// UI can show "Hold ESC 3s to return home"; `hint(false)` clears it.
void set_home_handler(void (*home)(void* ctx), void (*hint)(bool show, void* ctx), void* ctx);

// While a capture handler is set (e.g. a modal frequency dialog), every key
// is delivered to it raw and the normal nav/quit routing is bypassed. Pass
// nullptr to release the capture. `text` (device keyboard) delivers letters,
// space, comma and minus as characters, so F/X/Z/C type letters instead of
// acting as arrows: only for free-text entry, since menus that capture keys
// (the Radio hub) rely on F/X/Z/C as arrows.
void set_key_capture(void (*handler)(uint32_t key, void* ctx), void* ctx, bool text = false);

} // namespace platform
