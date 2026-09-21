/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "geo.h"

namespace toolkit {

// Minimal app config. `home` comes from the shared location
// (~/.config/zeroradio/location, set in Settings > Location) or the per-app env
// vars; the value below is only a neutral fallback until the user sets one (the
// apps ask for a position on first launch).
struct Config {
    geo::LatLon home{51.4779, -0.0015}; // Greenwich: fallback only
    double ttl_seconds{30.0};        // ADS-B: stale after ~30 s (radio-apps/01)
};

} // namespace toolkit
