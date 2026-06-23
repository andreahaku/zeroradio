/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "geo.h"

#include <algorithm>
#include <cmath>

namespace toolkit::geo {
namespace {

constexpr double kPi          = 3.14159265358979323846;
constexpr double kEarthRadNm  = 3440.065; // mean Earth radius in nautical miles

double deg2rad(double d) { return d * kPi / 180.0; }
double rad2deg(double r) { return r * 180.0 / kPi; }

bool valid(LatLon p) {
    if (std::isnan(p.lat) || std::isnan(p.lon)) {
        return false;
    }
    return p.lat >= -90.0 && p.lat <= 90.0 && p.lon >= -180.0 && p.lon <= 180.0;
}

} // namespace

double range_nm(LatLon home, LatLon p) {
    if (!valid(home) || !valid(p)) {
        return 0.0;
    }

    const double lat1 = deg2rad(home.lat);
    const double lat2 = deg2rad(p.lat);
    const double dlat = deg2rad(p.lat - home.lat);
    const double dlon = deg2rad(p.lon - home.lon);

    // Clamp against rounding: for near-antipodal points `a` can creep just past
    // 1.0, making sqrt(1-a) NaN and poisoning the range (which would then break
    // the sort comparator and reach lround(NaN) in project()).
    const double a = std::clamp(
        std::sin(dlat * 0.5) * std::sin(dlat * 0.5) +
            std::cos(lat1) * std::cos(lat2) * std::sin(dlon * 0.5) * std::sin(dlon * 0.5),
        0.0, 1.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadNm * c;
}

double bearing_deg(LatLon home, LatLon p) {
    if (!valid(home) || !valid(p)) {
        return 0.0;
    }

    const double lat1 = deg2rad(home.lat);
    const double lat2 = deg2rad(p.lat);
    const double dlon = deg2rad(p.lon - home.lon);

    const double y = std::sin(dlon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) -
                     std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
    double brg = rad2deg(std::atan2(y, x));
    brg = std::fmod(brg + 360.0, 360.0);
    return brg;
}

bool project(LatLon home,
             LatLon p,
             double range_rings_nm_max,
             int radius_px,
             int& out_dx,
             int& out_dy) {
    if (!valid(home) || !valid(p) || range_rings_nm_max <= 0.0 || radius_px <= 0) {
        return false;
    }

    const double range = range_nm(home, p);
    if (range > range_rings_nm_max) {
        return false;
    }

    const double brg = bearing_deg(home, p);
    const double r_px = (range / range_rings_nm_max) * static_cast<double>(radius_px);
    const double brg_rad = deg2rad(brg);

    // North-up: 0° points up (-y), 90° points right (+x).
    out_dx = static_cast<int>(std::lround(r_px * std::sin(brg_rad)));
    out_dy = static_cast<int>(std::lround(-r_px * std::cos(brg_rad)));
    return true;
}

} // namespace toolkit::geo
