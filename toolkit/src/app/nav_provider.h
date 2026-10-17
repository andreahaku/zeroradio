/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "i18n.h"

#include <string>

namespace toolkit {

// Drives the 5-key NavBar content. Implemented by each app's viewmodel. Slot 0
// is the page-number switcher, handled by the NavBar/Shell (it calls
// cycle_toolbar()); slots 1..4 are app-defined. A provider may set out[0] too,
// but the NavBar overrides slot 0's text with the 1-based page number.
class NavProvider {
public:
    virtual ~NavProvider() = default;

    struct NavSlot {
        const char* text{""};   // glyph (icon font) or short label; "" -> placeholder
        bool text_font{false};  // true -> render with the text font (labels/numbers)
        bool enabled{true};     // false -> shown greyed-out, not clickable
    };

    // Number of tool pages this app exposes (>= 1).
    virtual int nav_page_count() const = 0;

    // Fill out[1..4] for the given page (slot 0 is overridden by the NavBar).
    virtual void nav_fill(int page, NavSlot out[5]) const = 0;

    // Activate slot `slot` (1..4) on `page`. Slot 0 is handled by the NavBar.
    virtual void nav_activate(int page, int slot) = 0;

    // Short name of `page` (a few capitals, e.g. "TUNE"), shown on key 4 as a
    // tab, as in ZeroTracker. nullptr = the 1-based page number.
    virtual const char* nav_page_name(int page) const {
        (void)page;
        return nullptr;
    }

    // Whether slot `slot` (1..4) on `page` has an action for a long press, and
    // that action. It returns a short status to show, or "" for none.
    virtual bool nav_has_hold(int page, int slot) const {
        (void)page;
        (void)slot;
        return false;
    }
    virtual std::string nav_hold(int page, int slot) {
        (void)page;
        (void)slot;
        return {};
    }

    // What slot `slot` (1..4) does on `page`, for the key tooltip (SYM + 4-8).
    // Empty = no tooltip. Slot 0 is described by the shell.
    virtual i18n::Text nav_hint(int page, int slot) const {
        (void)page;
        (void)slot;
        return {};
    }
};

} // namespace toolkit
