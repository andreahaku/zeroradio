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

void apply_to_store(toolkit::EntityStore& store, const Vessel& v) {
    store.upsert(std::to_string(v.mmsi), [&v](toolkit::Entity& e) {
        // Sparse merge: only the fields this message carried (AIS spreads data
        // across message types). Absent fields keep their last value.
        if (v.msg_type >= 1 && v.msg_type <= 3) {
            if (v.has_pos) {
                e.has_pos = true;
                e.pos.lat = v.lat;
                e.pos.lon = v.lon;
            }
            if (v.has_sog) e.fields["sog"] = fmt1(v.sog);
            if (v.has_cog) e.fields["cog"] = fmt1(v.cog);
            if (v.has_hdg) e.fields["hdg"] = std::to_string(v.hdg);
            e.fields["status"] = std::to_string(v.nav_status);
        } else if (v.msg_type == 5) {
            if (!v.name.empty()) e.fields["name"] = v.name;
            if (!v.callsign.empty()) e.fields["callsign"] = v.callsign;
            if (!v.destination.empty()) e.fields["destination"] = v.destination;
            if (v.ship_type != 0) e.fields["shiptype"] = std::to_string(v.ship_type);
        }
        e.seen = 0.0; // a fresh sentence; the TTL sweep ages it from now
    });
}

void apply_nmea(AivdmReassembler& re, toolkit::EntityStore& store, const std::string& nmea) {
    std::string line;
    auto flush = [&]() {
        if (line.empty()) {
            return;
        }
        Vessel v;
        if (re.feed(line, v)) {
            apply_to_store(store, v);
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
}

} // namespace ais
