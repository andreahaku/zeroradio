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

// A NavBar key held this long runs its hold action, e.g. key 4 goes back one
// page (the ZeroTracker value).
constexpr uint32_t kNavHoldMs = 400;

// Auto-repeat: a key that steps a value (left/right, or a NavBar key the app
// asks for) runs again after kRepeatDelayMs held, then every kRepeatPeriodMs.
constexpr uint32_t kRepeatDelayMs = 400;
constexpr uint32_t kRepeatPeriodMs = 100;

// Key codes for the NavBar key tooltips: kKeyNavHint + slot (0..4). They sit in
// the Unicode private use area, clear of LVGL's control keys and of text.
constexpr uint32_t kKeyNavHint = 0xE000;

// Volume keys: Fn + A / S / D on the CardputerZero (KEY_MUTE, KEY_VOLUMEDOWN,
// KEY_VOLUMEUP), in the same private use area.
constexpr uint32_t kKeyMute = 0xE010;
constexpr uint32_t kKeyVolumeDown = 0xE011;
constexpr uint32_t kKeyVolumeUp = 0xE012;

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);
void register_nav_button(size_t index, lv_obj_t* button);
void unregister_nav_button(size_t index, lv_obj_t* button);

// ESC is decoupled from the nav keys and routed here (e.g. to quit). The '4'
// key still maps to nav button 0; ESC no longer does.
void set_quit_handler(void (*handler)(void* ctx), void* ctx);

// TAB (LV_KEY_NEXT) outside a key capture: app switch (SDR <-> Survey).
void set_tab_handler(void (*handler)(void* ctx), void* ctx);

// Arrows and Enter outside a key capture: LV_KEY_UP, LV_KEY_DOWN, LV_KEY_LEFT,
// LV_KEY_RIGHT or LV_KEY_ENTER. On the CardputerZero keyboard F/X/Z/C act as arrows without Fn.
void set_arrow_handler(void (*handler)(uint32_t key, void* ctx), void* ctx);

// NavBar keys 4..8 (slots 0..4) outside a key capture. When `has_hold(slot)`
// is true the key acts on release: a tap runs its normal action, a hold of
// kNavHoldMs calls `on_hold(slot)`. Other keys act on press, as before.
void set_nav_hold_handler(bool (*has_hold)(int slot, void* ctx),
                          void (*on_hold)(int slot, void* ctx), void* ctx);

// NavBar keys that auto-repeat while held (see kRepeatDelayMs), outside a key
// capture: `repeats(slot)` decides per slot. Left/right always repeat.
void set_nav_repeat_handler(bool (*repeats)(int slot, void* ctx), void* ctx);

// SYM + 4..8 outside a key capture (Shift + 4..8 on a desktop keyboard): show
// what NavBar slot `slot` (0..4) does.
void set_nav_hint_handler(void (*handler)(int slot, void* ctx), void* ctx);

// Volume keys outside a key capture: `step` is -1 (down), +1 (up) or 0 (mute).
void set_volume_handler(void (*handler)(int step, void* ctx), void* ctx);

// Called for every key press before it is routed, captures included, e.g. to
// close a tooltip.
void set_key_activity_handler(void (*handler)(uint32_t key, void* ctx), void* ctx);

// H outside a key capture: open the app's help page.
void set_help_handler(void (*handler)(void* ctx), void* ctx);

// M outside a key capture: save what the app is tuned to as a memory.
void set_memorize_handler(void (*handler)(void* ctx), void* ctx);

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
