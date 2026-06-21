/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "entity_store.h"

#include <string>
#include <vector>

namespace adsb {

// One aircraft as decoded by dump1090 / readsb (the `aircraft.json` shape),
// keyed by ICAO 24-bit `hex`. See radio-apps/01-adsb-1090.md for field mapping.
struct Aircraft {
    std::string hex;
    std::string flight;       // callsign, trimmed; empty -> omit
    bool has_pos{false};
    double lat{0.0};
    double lon{0.0};
    bool has_track{false};
    double track{0.0};        // degrees, for icon/heading
    bool has_alt{false};
    double alt_baro{0.0};     // ft; "ground" -> has_alt = false
    bool has_gs{false};
    double gs{0.0};           // kt
    std::string squawk;
    double seen{0.0};         // age in seconds
    std::string category;     // light/small/large/heavy/rotorcraft/other
    bool emergency{false};    // squawk in {7500, 7600, 7700}
};

// Parse a dump1090 `aircraft.json` document into Aircraft records. Tolerant of
// missing fields; never throws (malformed JSON yields an empty vector).
std::vector<Aircraft> parse_aircraft_json(const std::string& json);

// Apply the parsed aircraft into a generic EntityStore: pos + a string field
// bag (flight, alt, gs, track, squawk, category, emergency, seen) per aircraft.
void apply_to_store(toolkit::EntityStore& store, const std::vector<Aircraft>& aircraft);

} // namespace adsb
