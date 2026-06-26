/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_const.h"

#include <vector>

namespace radio {

// One launchable radio app. The hub menu renders `name`/`subtitle`/`icon`; the
// launcher resolves `bin` (the executable file name) using `app_dir` to walk the
// dev CMake tree (apps/<app_dir>/<config>/<bin>) and falls back to the install
// layout (all binaries colocated next to the hub). See app_launcher.cpp.
struct AppEntry {
    const char* id;        // stable slug, e.g. "sdr"
    const char* name;      // menu title, e.g. "SDR"
    const char* subtitle;  // one-line description under the title
    const char* icon;      // Phosphor-Fill glyph (see ui_const.h)
    const char* bin;        // executable file name, e.g. "sdr_app"
    const char* app_dir;   // source/build subdir under apps/, e.g. "sdr"
};

// The shipped suite, in menu order. Survey/SIGINT apps slot in here later.
// Icons are the verified-rendering glyphs already used by the SDR nav bar.
inline const std::vector<AppEntry>& app_catalog() {
    static const std::vector<AppEntry> kCatalog = {
        {"sdr", "SDR", "Spectrum & demod", view::ICON_BROADCAST, "sdr_app", "sdr"},
        {"adsb", "ADS-B", "Aircraft radar & map", view::ICON_MAP_TOGGLE, "adsb_app", "adsb"},
        {"meshtastic", "Meshtastic", "Mesh nodes & messages", view::ICON_BAND, "meshtastic_app", "meshtastic"},
    };
    return kCatalog;
}

} // namespace radio
