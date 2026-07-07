/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 *
 * Frozen parity test for the ADS-B dump1090 `aircraft.json` parser
 * (apps/adsb/src/model/aircraft — parse_aircraft_json). One canonical document
 * exercises every branch of the field map; the expected records are the frozen
 * oracle. This file is the immutable reward: it is not edited to make code pass.
 *
 * Oracle note: unlike the AIS decoder (bit-packed AIVDM, cross-checked with two
 * independent tools), dump1090 JSON is a direct, documented field->struct map,
 * so the oracle here is single-source (hand-authored against the dump1090 field
 * spec). The value is pinning the tricky branches: alt_baro number-vs-"ground"-
 * vs-absent, emergency-squawk detection, track wraparound, out-of-range position
 * rejection, category-code mapping, and the missing-hex skip.
 *
 * Coverage (one aircraft per branch, in document order; the no-hex record is
 * dropped, so it never appears in the parsed vector):
 *   a1  full moving record, track wraps 370->10, category A3->large
 *   b2  alt_baro "ground" -> on_ground (NOT has_alt), no position
 *   c3  emergency squawk 7700, position present
 *   (no hex) -> skipped entirely
 *   d4  lat out of range -> has_pos false, track -45->315, category A1->light
 *   e5  minimal (hex only) -> all optionals default/false
 *   f6  alt_baro null -> has_alt false, category A5->heavy, track -10->350
 *   g7  unknown category code A9 -> "other"
 */

#include "aircraft.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_fails = 0;

void check(bool ok, const char* name, const char* field) {
    if (!ok) {
        std::printf("  FAIL %-16s %s\n", name, field);
        ++g_fails;
    }
}

bool dclose(double a, double b) { return std::fabs(a - b) <= 1e-6; }

struct Expect {
    const char* name;
    const char* hex;
    const char* flight;
    bool has_pos; double lat; double lon;
    bool has_seen_pos; double seen_pos;
    bool has_track; double track;
    bool has_alt; double alt_baro; bool on_ground;
    bool has_gs; double gs;
    const char* squawk; bool emergency;
    bool has_seen; double seen;
    bool has_category; const char* category;
    bool has_rssi; double rssi;
};

// The frozen oracle: the parsed records the document below must yield, in order.
const Expect kExpect[] = {
    {"a1_full", "a1", "DLH441", true, 47.5, 8.5, true, 2.1, true, 10.0,
     true, 35000.0, false, true, 450.0, "1000", false, true, 0.5, true, "large", true, -12.3},
    {"b2_ground", "b2", "SWR", false, 0.0, 0.0, false, 0.0, false, 0.0,
     false, 0.0, true, true, 5.0, "2000", false, false, 0.0, false, "", false, 0.0},
    {"c3_emerg", "c3", "", true, 51.0, 0.0, false, 0.0, false, 0.0,
     false, 0.0, false, false, 0.0, "7700", true, false, 0.0, false, "", false, 0.0},
    {"d4_nopos", "d4", "", false, 0.0, 0.0, false, 0.0, true, 315.0,
     false, 0.0, false, false, 0.0, "", false, false, 0.0, true, "light", false, 0.0},
    {"e5_min", "e5", "", false, 0.0, 0.0, false, 0.0, false, 0.0,
     false, 0.0, false, false, 0.0, "", false, false, 0.0, false, "", false, 0.0},
    {"f6_altnull", "f6", "", false, 0.0, 0.0, false, 0.0, true, 350.0,
     false, 0.0, false, false, 0.0, "", false, false, 0.0, true, "heavy", false, 0.0},
    {"g7_catother", "g7", "", false, 0.0, 0.0, false, 0.0, false, 0.0,
     false, 0.0, false, false, 0.0, "", false, false, 0.0, true, "other", false, 0.0},
    // h8: squawk 7500 is also an emergency; category A7 -> rotorcraft.
    {"h8_hijack", "h8", "", false, 0.0, 0.0, false, 0.0, false, 0.0,
     false, 0.0, false, false, 0.0, "7500", true, false, 0.0, true, "rotorcraft", false, 0.0},
    // i9: squawk 7600 (radio failure) emergency; category A2 -> small; only lon in
    // range (lat out of bounds) so the whole position is rejected.
    {"i9_radiofail", "i9", "", false, 0.0, 0.0, false, 0.0, false, 0.0,
     false, 0.0, false, false, 0.0, "7600", true, false, 0.0, true, "small", false, 0.0},
    // j0: a non-emergency squawk near the emergency range must NOT trip emergency;
    // track exactly 360 normalizes to 0; category A1 -> light.
    {"j0_edge", "j0", "", false, 0.0, 0.0, false, 0.0, true, 0.0,
     false, 0.0, false, false, 0.0, "7699", false, false, 0.0, true, "light", false, 0.0},
};

const char* kDoc = R"JSON({
  "now": 1720000000.0,
  "aircraft": [
    {"hex":"a1","flight":"DLH441 ","lat":47.5,"lon":8.5,"seen_pos":2.1,"track":370.0,
     "alt_baro":35000,"gs":450,"squawk":"1000","category":"A3","seen":0.5,"rssi":-12.3},
    {"hex":"b2","flight":"SWR","alt_baro":"ground","gs":5,"squawk":"2000"},
    {"hex":"c3","lat":51.0,"lon":0.0,"squawk":"7700"},
    {"flight":"GHOST","lat":10.0,"lon":10.0},
    {"hex":"d4","lat":200.0,"lon":8.0,"track":-45.0,"category":"A1"},
    {"hex":"e5"},
    {"hex":"f6","alt_baro":null,"track":-10.0,"category":"a5"},
    {"hex":"g7","category":"A9"},
    {"hex":"h8","squawk":"7500","category":"A7"},
    {"hex":"i9","lat":200.0,"lon":9.0,"squawk":"7600","category":"A2"},
    {"hex":"j0","squawk":"7699","track":360.0,"category":"A1"}
  ]
})JSON";

} // namespace

using namespace adsb;

int main() {
    const std::vector<Aircraft> got = parse_aircraft_json(kDoc);

    const size_t expected_n = sizeof(kExpect) / sizeof(kExpect[0]);
    if (got.size() != expected_n) {
        std::printf("Aircraft parse: got %zu records, expected %zu (no-hex record must be skipped)\n",
                    got.size(), expected_n);
        return 1;
    }

    for (size_t i = 0; i < expected_n; ++i) {
        const Expect& e = kExpect[i];
        const Aircraft& a = got[i];
        check(a.hex == e.hex, e.name, "hex");
        check(a.flight == e.flight, e.name, "flight");
        check(a.has_pos == e.has_pos, e.name, "has_pos");
        if (e.has_pos) {
            check(dclose(a.lat, e.lat), e.name, "lat");
            check(dclose(a.lon, e.lon), e.name, "lon");
        }
        check(a.has_seen_pos == e.has_seen_pos, e.name, "has_seen_pos");
        if (e.has_seen_pos) check(dclose(a.seen_pos, e.seen_pos), e.name, "seen_pos");
        check(a.has_track == e.has_track, e.name, "has_track");
        if (e.has_track) check(dclose(a.track, e.track), e.name, "track");
        check(a.has_alt == e.has_alt, e.name, "has_alt");
        if (e.has_alt) check(dclose(a.alt_baro, e.alt_baro), e.name, "alt_baro");
        check(a.on_ground == e.on_ground, e.name, "on_ground");
        check(a.has_gs == e.has_gs, e.name, "has_gs");
        if (e.has_gs) check(dclose(a.gs, e.gs), e.name, "gs");
        check(a.squawk == e.squawk, e.name, "squawk");
        check(a.emergency == e.emergency, e.name, "emergency");
        check(a.has_seen == e.has_seen, e.name, "has_seen");
        if (e.has_seen) check(dclose(a.seen, e.seen), e.name, "seen");
        check(a.has_category == e.has_category, e.name, "has_category");
        if (e.has_category) check(a.category == e.category, e.name, "category");
        check(a.has_rssi == e.has_rssi, e.name, "has_rssi");
        if (e.has_rssi) check(dclose(a.rssi, e.rssi), e.name, "rssi");
    }

    // Malformed input must never throw: it yields an empty vector.
    check(parse_aircraft_json("not json at all").empty(), "malformed", "empty on garbage");
    check(parse_aircraft_json("{}").empty(), "no_aircraft", "empty when no aircraft array");
    check(parse_aircraft_json("").empty(), "empty_input", "empty on empty string");

    if (g_fails == 0) {
        std::printf("Aircraft parse: %zu records + malformed guards OK\n", expected_n);
        return 0;
    }
    std::printf("Aircraft parse: %d assertion(s) FAILED\n", g_fails);
    return 1;
}
