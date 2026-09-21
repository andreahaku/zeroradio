/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// toolkit::location: coordinate parsing, city search on the shipped list, and
// the save/load round trip of the shared position.

#include "location.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef CITIES_TSV
#define CITIES_TSV "assets/geodata/cities.tsv"
#endif

using namespace toolkit;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

int main() {
    // Coordinates.
    auto c = location::parse_coords("35.9, 14.51");
    check(c && std::fabs(c->lat - 35.9) < 1e-9 && std::fabs(c->lon - 14.51) < 1e-9, "comma pair");
    c = location::parse_coords("  -33.87 151.21 ");
    check(c && c->lat < 0 && c->lon > 151, "space pair, negative");
    check(!location::parse_coords("valletta"), "a name is not coordinates");
    check(!location::parse_coords("95, 10"), "latitude out of range");
    check(!location::parse_coords("10, 200"), "longitude out of range");
    check(!location::parse_coords("10, 20, 30"), "three numbers");

    // City search on the real list.
    const auto idx = location::CityIndex::load(CITIES_TSV);
    check(idx.valid(), "city list loads");
    auto r = idx.search("valletta", 5);
    check(!r.empty() && r[0].label == "Valletta, MT", "valletta found first");
    check(!r.empty() && std::fabs(r[0].pos.lat - 35.9) < 0.01, "valletta coordinates");
    r = idx.search("york", 5);
    bool ny = false;
    for (const auto& p : r) ny = ny || p.label == "New York City, US";
    check(ny, "word prefix: york -> New York City");
    r = idx.search("CESE", 3);
    check(!r.empty() && r[0].label == "Bologna, IT", "case-insensitive prefix");
    check(idx.search("", 5).empty(), "empty query");
    check(idx.search("zzzzqqq", 5).empty(), "no match");

    // NMEA.
    const auto fix = location::parse_rmc(
        "$GNRMC,123519.00,A,3554.000,N,01430.900,E,0.1,0.0,210926,,,A*6A");
    check(fix && std::fabs(fix->lat - 35.9) < 1e-6 && std::fabs(fix->lon - 14.515) < 1e-6,
          "RMC valid fix");
    check(!location::parse_rmc("$GNRMC,,V,,,,,,,,,,N,V*37"), "RMC void (no fix)");
    const auto west = location::parse_rmc("$GPRMC,1,A,3352.20,S,15112.60,W,0,0,1,,,A*00");
    check(west && west->lat < 0 && west->lon < 0, "RMC south/west signs");
    const auto sats = location::parse_gga_satellites("$GNGGA,,,,,,0,07,25.5,,,,,,*64");
    check(sats && *sats == 7, "GGA satellites");
    check(!location::parse_gga_satellites("$GNRMC,,V,,,,,,,,,,N,V*37"), "GGA parser ignores RMC");

    // Save / load via a private config dir.
    char tmpl[] = "/tmp/zeroradio-loc-XXXXXX";
    const char* dir = mkdtemp(tmpl);
    check(dir != nullptr, "temp dir");
    if (dir) {
        setenv("XDG_CONFIG_HOME", dir, 1);
        check(!location::load(), "unset before save");
        check(location::save({{35.9, 14.515}, "Valletta, MT"}), "save");
        const auto back = location::load();
        check(back && back->label == "Valletta, MT" && std::fabs(back->pos.lon - 14.515) < 1e-6,
              "load round trip");
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
