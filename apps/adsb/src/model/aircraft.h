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
    bool has_seen_pos{false}; // dump1090 reported an age for the position
    double seen_pos{0.0};     // seconds since the position was last updated
    bool has_track{false};
    double track{0.0};        // degrees, for icon/heading
    bool has_alt{false};      // a numeric barometric altitude was reported
    double alt_baro{0.0};     // ft (valid only when has_alt)
    bool on_ground{false};    // alt_baro reported the string "ground" (distinct from unknown)
    bool has_gs{false};
    double gs{0.0};           // kt
    std::string squawk;       // empty -> not reported in this message
    bool has_seen{false};
    double seen{0.0};         // age in seconds (valid only when has_seen)
    bool has_category{false}; // the emitter "category" field was present
    std::string category;     // light/small/large/heavy/rotorcraft/other
    bool emergency{false};    // squawk in {7500, 7600, 7700} (only meaningful with squawk)
    bool has_rssi{false};
    double rssi{0.0};         // recent average signal power, dBFS (valid only when has_rssi)
};

// Parse a dump1090 `aircraft.json` document into Aircraft records. Tolerant of
// missing fields; never throws (malformed JSON yields an empty vector).
std::vector<Aircraft> parse_aircraft_json(const std::string& json);

// Apply the parsed aircraft into a generic EntityStore: pos + a string field
// bag (flight, alt, gs, track, squawk, category, emergency, seen) per aircraft.
void apply_to_store(toolkit::EntityStore& store, const std::vector<Aircraft>& aircraft);

} // namespace adsb
