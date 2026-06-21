/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

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
};

} // namespace toolkit
