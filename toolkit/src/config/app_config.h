/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "geo.h"

namespace toolkit {

// Minimal app config. Hardcoded defaults are fine for the mock build; a real
// deployment would load these from ~/.config/<app>/config (home lat/lon, units,
// ring ladder, TTL) — see radio-apps/00-viewer-toolkit.md. The HOME default is
// the Milano area, matching the bundled mock aircraft.json.
struct Config {
    geo::LatLon home{45.46, 9.19};
    double ttl_seconds{30.0}; // ADS-B: stale after ~30 s (radio-apps/01)
};

} // namespace toolkit
