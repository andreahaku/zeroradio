/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace toolkit::geo {

struct LatLon {
    double lat{0.0};
    double lon{0.0};
};

// Great-circle distance between two points, in nautical miles (haversine).
double range_nm(LatLon home, LatLon p);

// Initial bearing from `home` to `p`, normalized to [0, 359] degrees (0 = N).
double bearing_deg(LatLon home, LatLon p);

// Project `p` onto a north-up PPI plot centred on `home`: maps (bearing, range)
// to a screen offset from the centre (dx positive right, dy positive down), for
// a plot of `radius_px` whose outer ring is `range_rings_nm_max` NM. Returns
// false (and leaves out_dx/out_dy untouched) if either input is invalid or the
// point lies beyond the maximum range. Bearings are clamped to 0..359.
bool project(LatLon home,
             LatLon p,
             double range_rings_nm_max,
             int radius_px,
             int& out_dx,
             int& out_dy);

} // namespace toolkit::geo
