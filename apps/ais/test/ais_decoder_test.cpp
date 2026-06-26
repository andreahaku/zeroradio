/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 *
 * Frozen AIS decoder oracle. Every expected value below was produced by encoding
 * the field set with pyais and decoding the resulting !AIVDM sentence with BOTH
 * gpsd's `gpsdecode` AND pyais, requiring the two independent decoders to agree
 * (see scratchpad gen_oracle2.py). The vectors cover the classic decode traps:
 *   - V1: northern/eastern position, SOG/COG scaling (1/10 kt, 1/10 deg)
 *   - V2: message TYPE 3 (shared Common Navigation Block dispatch)
 *   - V3: southern/western position -> two's-complement sign on lat(27b)+lon(28b)
 *   - V4: "not available" sentinels: lon=181, lat=91, SOG=1023, COG=3600, HDG=511
 *   - V5: a real-world canonical sentence (gpsd doc), negative longitude
 * Across the set the 6-bit de-armoring exercises both the plain and the -8 branch.
 * This file is the immutable reward oracle: it is not edited to make code pass.
 */

#include "ais_decoder.h"

#include <cmath>
#include <cstdio>

namespace {

struct Expect {
    const char* name;
    const char* sentence;
    int type;
    unsigned mmsi;
    bool has_pos; double lat; double lon;
    bool has_sog; double sog;
    bool has_cog; double cog;
    bool has_hdg; int hdg;
    int nav_status;
};

// === AIS oracle (frozen, two-source gpsdecode+pyais). Generated, do not edit by hand. ===
const Expect kVectors[] = {
    {"V1_NE_moving", "!AIVDM,1,1,,A,11mg=5@P1s157m0EMB`3Ojl1P000,0*19", 1, 123456789u, true, 37.5000000, 15.1000000, true, 12.3, true, 89.5, true, 90, 0},
    {"V2_type3", "!AIVDM,1,1,,A,33co>HPP000dwpPIIvP8=6f1P000,0*7D", 3, 247320162u, true, 44.4000000, 9.8300000, true, 0.0, true, 210.0, true, 215, 0},
    {"V3_SW_negative", "!AIVDM,1,1,,A,1>eq`d@P1@KlkJQd<r@:S8N1P000,0*2B", 1, 987654321u, true, -34.6000000, -58.3700000, true, 8.0, true, 270.0, true, 271, 0},
    {"V4_sentinel", "!AIVDM,1,1,,A,11b4N?OP?w<tSF0l4Q@>4?v1P000,0*60", 1, 111222333u, false, 0.0000000, 0.0000000, false, 0.0, false, 0.0, false, 0, 15},
    {"V5_canonical_real", "!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C", 1, 477553000u, true, 47.5828330, -122.3458330, true, 0.0, true, 51.0, true, 181, 5},
};

int g_fails = 0;

void check(bool ok, const char* name, const char* field) {
    if (!ok) {
        std::printf("  FAIL %-18s %s\n", name, field);
        ++g_fails;
    }
}

bool close(double a, double b) { return std::fabs(a - b) <= 5e-5; }

} // namespace

int main() {
    for (const auto& e : kVectors) {
        ais::Vessel v;
        const bool ok = ais::parse_aivdm(e.sentence, v);
        check(ok, e.name, "parse_aivdm returned false");
        if (!ok) {
            continue;
        }
        check(v.msg_type == e.type, e.name, "msg_type");
        check(v.mmsi == e.mmsi, e.name, "mmsi");
        check(v.nav_status == e.nav_status, e.name, "nav_status");

        check(v.has_pos == e.has_pos, e.name, "has_pos");
        if (e.has_pos) {
            check(close(v.lat, e.lat), e.name, "lat");
            check(close(v.lon, e.lon), e.name, "lon");
        }
        check(v.has_sog == e.has_sog, e.name, "has_sog");
        if (e.has_sog) {
            check(close(v.sog, e.sog), e.name, "sog");
        }
        check(v.has_cog == e.has_cog, e.name, "has_cog");
        if (e.has_cog) {
            check(close(v.cog, e.cog), e.name, "cog");
        }
        check(v.has_hdg == e.has_hdg, e.name, "has_hdg");
        if (e.has_hdg) {
            check(v.hdg == e.hdg, e.name, "hdg");
        }
    }

    // Malformed inputs must be rejected (all carry a VALID checksum so each
    // isolates one validation path, not the checksum). Frozen, two-source style.
    ais::Vessel junk;
    check(!ais::parse_aivdm("!AIVDM,1,1,,A,11mg=5@P1s157m0EMB`3Ojl1P000,0*00", junk),
          "bad_checksum", "should reject");
    // Truncated payload (120 bits < the 168 a type 1/2/3 must carry).
    check(!ais::parse_aivdm("!AIVDM,1,1,,A,11mg=5@P1s157m0EMB`3,0*01", junk),
          "truncated_payload", "should reject");
    // 'X' (0x58) sits in the illegal 6-bit armor gap 'X'-'_'.
    check(!ais::parse_aivdm("!AIVDM,1,1,,A,11mg=5@P1s157m0EMBX3Ojl1P000,0*21", junk),
          "armor_gap_char", "should reject");
    // A 28-char payload that declares fill=1 claims 167 effective bits, not the
    // 168 a type 1/2/3 frame must carry.
    check(!ais::parse_aivdm("!AIVDM,1,1,,A,11mg=5@P1s157m0EMB`3Ojl1P000,1*18", junk),
          "bad_fill_bits", "should reject");

    const int total = static_cast<int>(sizeof(kVectors) / sizeof(kVectors[0]));
    if (g_fails == 0) {
        std::printf("AIS decoder: %d/%d vectors OK\n", total, total);
        return 0;
    }
    std::printf("AIS decoder: %d assertion(s) FAILED\n", g_fails);
    return 1;
}
