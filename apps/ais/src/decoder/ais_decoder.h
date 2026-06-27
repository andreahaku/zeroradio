/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <map>
#include <string>

namespace ais {

// One vessel as decoded from AIS reports. The dynamic fields come from a position
// report (types 1/2/3, Common Navigation Block); the static fields come from a
// static/voyage report (type 5, a multi-fragment message). A single Vessel never
// carries both — they are merged per MMSI in the EntityStore. Explicit has_*
// flags (and empty strings / ship_type 0) mean a "not available" sentinel becomes
// absence, never a bogus 181°/1023 value.
struct Vessel {
    int msg_type{0};        // 1, 2, 3 (position) or 5 (static)
    unsigned mmsi{0};

    // --- dynamic (types 1/2/3) ---
    bool has_pos{false};
    double lat{0.0};        // degrees, +N (sentinel 91° -> has_pos=false)
    double lon{0.0};        // degrees, +E (sentinel 181° -> has_pos=false)
    bool has_sog{false};
    double sog{0.0};        // knots (sentinel 1023 -> absent)
    bool has_cog{false};
    double cog{0.0};        // degrees (sentinel 3600 -> absent)
    bool has_hdg{false};
    int hdg{0};             // degrees 0-359 (sentinel 511 -> absent)
    int nav_status{15};     // 0-15 (15 = not defined)

    // --- static / voyage (type 5) ---
    std::string name;        // vessel name, trimmed (empty = absent)
    std::string callsign;    // radio callsign, trimmed (empty = absent)
    std::string destination; // voyage destination, trimmed (empty = absent)
    int ship_type{0};        // AIS ship-and-cargo type code (0 = not available)
};

// Decode a single-fragment AIVDM/AIVDO position report (type 1/2/3) into `out`.
// Returns false for a malformed sentence, a bad checksum, a multi-fragment
// sentence, a wrong length, or a type outside {1,2,3}. Tolerant: never throws.
// (Use AivdmReassembler for multi-fragment messages and type 5.)
bool parse_aivdm(const std::string& sentence, Vessel& out);

// Stateful AIVDM decoder that reassembles multi-fragment sentences (required for
// type 5, which spans two fragments) before decoding. feed() one NMEA line at a
// time; it returns true and fills `out` when a line completes a decodable message
// (position 1/2/3 or static 5), false for an incomplete fragment, a bad/undecoded
// sentence, or an unsupported type. Fragments are keyed by channel + sequence id,
// so interleaved multipart messages on the two AIS channels reassemble correctly.
class AivdmReassembler {
public:
    bool feed(const std::string& sentence, Vessel& out);

private:
    struct Partial {
        int total{0};
        int next{1};          // expected next fragment number
        std::string payload;  // concatenated armored payloads so far
        int last_fill{0};
    };
    std::map<std::string, Partial> partials_;
};

// Map an AIS ship-and-cargo type code to a short human category, e.g. 70-79 ->
// "Cargo", 80-89 -> "Tanker", 60-69 -> "Passenger". Returns "" for 0/unknown.
const char* ship_type_label(int code);

} // namespace ais
