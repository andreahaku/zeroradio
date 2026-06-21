/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "geo.h"

namespace toolkit {

// Minimal app config. The HOME default can be overridden at runtime (env vars in
// main.cpp; a Settings screen later). A real deployment would load these from
// ~/.config/<app>/config (home lat/lon, units, ring ladder, TTL) — see
// radio-apps/00-viewer-toolkit.md.
struct Config {
    geo::LatLon home{44.49, 11.34}; // Bologna, IT
    double ttl_seconds{30.0};        // ADS-B: stale after ~30 s (radio-apps/01)
};

} // namespace toolkit
