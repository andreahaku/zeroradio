/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace ais {

// One vessel as decoded from an AIS position report (message types 1/2/3, the
// Common Navigation Block). Mirrors apps/adsb's Aircraft: explicit has_* flags
// so a "not available" sentinel becomes absence, never a bogus 181°/1023 value.
struct Vessel {
    int msg_type{0};        // 1, 2 or 3
    unsigned mmsi{0};       // 30-bit Maritime Mobile Service Identity

    bool has_pos{false};
    double lat{0.0};        // degrees, +N (sentinel 91° -> has_pos=false)
    double lon{0.0};        // degrees, +E (sentinel 181° -> has_pos=false)

    bool has_sog{false};
    double sog{0.0};        // speed over ground, knots (sentinel 1023 -> absent)

    bool has_cog{false};
    double cog{0.0};        // course over ground, degrees (sentinel 360.0 -> absent)

    bool has_hdg{false};
    int hdg{0};             // true heading, degrees 0-359 (sentinel 511 -> absent)

    int nav_status{15};     // 0-15 navigation status (15 = not defined)
};

// Decode a single-fragment AIVDM/AIVDO position report (message type 1, 2 or 3)
// into `out`. Returns true on success; false for a malformed sentence, a bad
// NMEA checksum, a multi-fragment sentence, a too-short payload, or a message
// type outside {1,2,3}. Tolerant: never throws.
bool parse_aivdm(const std::string& sentence, Vessel& out);

} // namespace ais
