/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace platform {

constexpr size_t kNavKeyCount = 5;

// ESC hold: the hint appears after kEscHoldHintMs, the app leaves for the
// system launcher after kEscHoldHomeMs.
constexpr uint32_t kEscHoldHintMs = 300;
constexpr uint32_t kEscHoldHomeMs = 3000;

// Key 4 held this long goes back one NavBar page (the ZeroTracker value).
constexpr uint32_t kPageHoldMs = 400;

// Key codes for the NavBar key tooltips: kKeyNavHint + slot (0..4). They sit in
// the Unicode private use area, clear of LVGL's control keys and of text.
constexpr uint32_t kKeyNavHint = 0xE000;

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);
void register_nav_button(size_t index, lv_obj_t* button);
void unregister_nav_button(size_t index, lv_obj_t* button);

// ESC is decoupled from the nav keys and routed here (e.g. to quit). The '4'
// key still maps to nav button 0; ESC no longer does.
void set_quit_handler(void (*handler)(void* ctx), void* ctx);

// TAB (LV_KEY_NEXT) outside a key capture: app switch (SDR <-> Survey).
void set_tab_handler(void (*handler)(void* ctx), void* ctx);

// Arrows outside a key capture: LV_KEY_UP, LV_KEY_DOWN, LV_KEY_LEFT or
// LV_KEY_RIGHT. On the CardputerZero keyboard F/X/Z/C act as arrows without Fn.
void set_arrow_handler(void (*handler)(uint32_t key, void* ctx), void* ctx);

// Key 4 outside a key capture, once a handler is set: a tap shows the next
// NavBar page on release, a hold of kPageHoldMs calls `handler` (the page
// before). Without a handler key 4 acts on press, as before.
void set_page_back_handler(void (*handler)(void* ctx), void* ctx);

// SYM + 4..8 outside a key capture (Shift + 4..8 on a desktop keyboard): show
// what NavBar slot `slot` (0..4) does.
void set_nav_hint_handler(void (*handler)(int slot, void* ctx), void* ctx);

// Called for every key press before it is routed, captures included, e.g. to
// close a tooltip.
void set_key_activity_handler(void (*handler)(uint32_t key, void* ctx), void* ctx);

// H outside a key capture: open the app's help page.
void set_help_handler(void (*handler)(void* ctx), void* ctx);

// ESC outside a key capture follows the CardputerZero convention: a short press
// is the normal quit/back (set_quit_handler), holding it kEscHoldHomeMs (3 s)
// returns to the system launcher (`home`). `hint(true)` fires once the hold
// passes kEscHoldHintMs so the UI can show the exit prompt; `hint(false)`
// clears it. Releasing ESC while the prompt is up cancels the hold.
void set_home_handler(void (*home)(void* ctx), void (*hint)(bool show, void* ctx), void* ctx);

// While a capture handler is set (e.g. a modal frequency dialog), every key
// is delivered to it raw and the normal nav/quit routing is bypassed. Pass
// nullptr to release the capture. `text` (device keyboard) delivers letters,
// space, comma and minus as characters, so F/X/Z/C type letters instead of
// acting as arrows: only for free-text entry, since menus that capture keys
// (the Radio hub) rely on F/X/Z/C as arrows.
void set_key_capture(void (*handler)(uint32_t key, void* ctx), void* ctx, bool text = false);

} // namespace platform
