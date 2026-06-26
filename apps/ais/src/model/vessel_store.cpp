/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "vessel_store.h"

#include <cstdio>

namespace ais {
namespace {

std::string fmt1(double v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

} // namespace

std::vector<Vessel> parse_nmea_lines(const std::string& nmea) {
    std::vector<Vessel> out;
    std::string line;
    auto flush = [&]() {
        if (line.empty()) {
            return;
        }
        Vessel v;
        if (parse_aivdm(line, v)) {
            out.push_back(v);
        }
        line.clear();
    };
    for (char c : nmea) {
        if (c == '\n' || c == '\r') {
            flush();
        } else {
            line.push_back(c);
        }
    }
    flush();
    return out;
}

void apply_to_store(toolkit::EntityStore& store, const std::vector<Vessel>& vessels) {
    for (const auto& v : vessels) {
        store.upsert(std::to_string(v.mmsi), [&v](toolkit::Entity& e) {
            if (v.has_pos) {
                e.has_pos = true;
                e.pos.lat = v.lat;
                e.pos.lon = v.lon;
            }
            // Sparse merge: only fields this message carried (AIS spreads data
            // across message types). Absent fields keep their last value.
            if (v.has_sog) {
                e.fields["sog"] = fmt1(v.sog);
            }
            if (v.has_cog) {
                e.fields["cog"] = fmt1(v.cog);
            }
            if (v.has_hdg) {
                e.fields["hdg"] = std::to_string(v.hdg);
            }
            e.fields["status"] = std::to_string(v.nav_status);
            e.fields["type"] = std::to_string(v.msg_type);
            e.seen = 0.0; // a fresh sentence; the TTL sweep ages it from now
        });
    }
}

} // namespace ais
