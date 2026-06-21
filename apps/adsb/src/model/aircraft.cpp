/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "aircraft.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>

namespace adsb {
namespace {

using json = nlohmann::json;

std::string trim(const std::string& s) {
    auto begin = s.begin();
    auto end = s.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

// Map the ADS-B emitter category code (e.g. "A3") to a coarse class. dump1090
// emits uppercase, but normalize so a lowercase feed doesn't fall through to
// "other".
std::string category_from_emitter(std::string code) {
    std::transform(code.begin(), code.end(), code.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (code == "A1") return "light";
    if (code == "A2") return "small";
    if (code == "A3" || code == "A4") return "large";
    if (code == "A5") return "heavy";
    if (code == "A7") return "rotorcraft";
    return "other";
}

bool is_emergency_squawk(const std::string& squawk) {
    return squawk == "7500" || squawk == "7600" || squawk == "7700";
}

// Read a numeric JSON value as a finite double, tolerating numbers and numeric
// strings. Rejects null, partial parses ("123junk") and non-finite ("nan"/"inf").
bool read_number(const json& node, const char* key, double& out) {
    auto it = node.find(key);
    if (it == node.end() || it->is_null()) {
        return false;
    }

    double v = 0.0;
    if (it->is_number()) {
        v = it->get<double>();
    } else if (it->is_string()) {
        const std::string s = trim(it->get<std::string>());
        if (s.empty()) {
            return false;
        }
        try {
            size_t consumed = 0;
            v = std::stod(s, &consumed);
            if (consumed != s.size()) {
                return false; // trailing junk after the number
            }
        } catch (...) {
            return false;
        }
    } else {
        return false;
    }

    if (!std::isfinite(v)) {
        return false;
    }
    out = v;
    return true;
}

// Safe double -> long long: guards the cast against non-finite / out-of-range
// values (which would be undefined behaviour).
bool to_ll(double v, long long& out) {
    // LLONG_MAX as a double rounds *up* to 2^63, so `v > (double)LLONG_MAX`
    // would let exactly 2^63 through and then cast it out of range (UB). Test the
    // half-open range [-2^63, 2^63): LLONG_MIN is an exact power of two as a
    // double, so -(double)LLONG_MIN == 2^63 with no rounding.
    if (!std::isfinite(v) ||
        v < static_cast<double>(std::numeric_limits<long long>::min()) ||
        v >= -static_cast<double>(std::numeric_limits<long long>::min())) {
        return false;
    }
    out = static_cast<long long>(v);
    return true;
}

std::string read_string(const json& node, const char* key) {
    auto it = node.find(key);
    if (it == node.end()) {
        return {};
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<long long>());
    }
    return {};
}

} // namespace

std::vector<Aircraft> parse_aircraft_json(const std::string& json_text) {
    std::vector<Aircraft> result;

    json doc = json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return result;
    }

    auto list = doc.find("aircraft");
    if (list == doc.end() || !list->is_array()) {
        return result;
    }

    for (const auto& node : *list) {
        if (!node.is_object()) {
            continue;
        }

        Aircraft ac;
        ac.hex = trim(read_string(node, "hex"));
        if (ac.hex.empty()) {
            continue; // hex is the entity key; skip records without it
        }

        ac.flight = trim(read_string(node, "flight"));

        // Position: both lat/lon must be present and within bounds.
        double lat = 0.0;
        double lon = 0.0;
        if (read_number(node, "lat", lat) && read_number(node, "lon", lon) &&
            !std::isnan(lat) && !std::isnan(lon) &&
            lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
            ac.has_pos = true;
            ac.lat = lat;
            ac.lon = lon;
        }

        // Position age (dump1090's `seen_pos`): lets the viewer drop a stale
        // position even while the aircraft itself is still being heard.
        double seen_pos = 0.0;
        if (read_number(node, "seen_pos", seen_pos)) {
            ac.has_seen_pos = true;
            ac.seen_pos = seen_pos;
        }

        double track = 0.0;
        if (read_number(node, "track", track)) {
            ac.has_track = true;
            ac.track = std::fmod(std::fmod(track, 360.0) + 360.0, 360.0);
        }

        // alt_baro may be a number, the string "ground", or absent. "ground" and
        // "absent" are distinct states: on_ground vs unknown.
        auto alt_it = node.find("alt_baro");
        if (alt_it != node.end() && !alt_it->is_null()) {
            if (alt_it->is_number()) {
                const double a = alt_it->get<double>();
                if (std::isfinite(a)) {
                    ac.has_alt = true;
                    ac.alt_baro = a;
                }
            } else if (alt_it->is_string()) {
                std::string a = trim(alt_it->get<std::string>());
                std::transform(a.begin(), a.end(), a.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (a == "ground") {
                    ac.on_ground = true;
                }
            }
        }

        double gs = 0.0;
        if (read_number(node, "gs", gs)) {
            ac.has_gs = true;
            ac.gs = gs;
        }

        ac.squawk = trim(read_string(node, "squawk"));
        ac.emergency = is_emergency_squawk(ac.squawk);

        double seen = 0.0;
        if (read_number(node, "seen", seen)) {
            ac.has_seen = true;
            ac.seen = seen;
        }

        const std::string emitter = trim(read_string(node, "category"));
        if (!emitter.empty()) {
            ac.has_category = true;
            ac.category = category_from_emitter(emitter);
        }

        double rssi = 0.0;
        if (read_number(node, "rssi", rssi)) {
            ac.has_rssi = true;
            ac.rssi = rssi;
        }

        result.push_back(std::move(ac));
    }

    return result;
}

void apply_to_store(toolkit::EntityStore& store, const std::vector<Aircraft>& aircraft) {
    for (const auto& ac : aircraft) {
        // EntityStore runs this patch and snapshot() under one mutex, so these
        // multi-field updates are observed atomically by the UI thread.
        store.upsert(ac.hex, [&ac](toolkit::Entity& e) {
            if (ac.has_pos) {
                e.has_pos = true;
                e.pos.lat = ac.lat;
                e.pos.lon = ac.lon;
            }

            // Sparse merge: only overwrite fields this message actually carried,
            // so updates accumulate across ADS-B message types. Absent fields keep
            // their last-known value; vanished aircraft are dropped by the TTL
            // sweep (we don't retire individual fields when the feed omits them).
            if (!ac.flight.empty()) {
                e.fields["flight"] = ac.flight;
            }

            long long ll = 0;
            // Altitude and ground are distinct states; writing one clears the other.
            if (ac.has_alt && to_ll(ac.alt_baro, ll)) {
                e.fields["alt"] = std::to_string(ll);
                e.fields.erase("on_ground");
            } else if (ac.on_ground) {
                e.fields["on_ground"] = "1";
                e.fields.erase("alt");
            }
            if (ac.has_gs && to_ll(ac.gs, ll)) {
                e.fields["gs"] = std::to_string(ll);
            }
            if (ac.has_track && to_ll(ac.track, ll)) {
                e.fields["track"] = std::to_string(ll);
            }
            // Squawk and the derived emergency flag travel together, so a return
            // to a normal code clears a previous 7500/7600/7700 alert.
            if (!ac.squawk.empty()) {
                e.fields["squawk"] = ac.squawk;
                e.fields["emergency"] = ac.emergency ? "1" : "0";
            }
            if (ac.has_category) {
                e.fields["category"] = ac.category;
            }
            if (ac.has_rssi) {
                char b[16];
                std::snprintf(b, sizeof(b), "%.1f", ac.rssi);
                e.fields["rssi"] = b;
            }
            if (ac.has_seen && to_ll(ac.seen, ll)) {
                e.seen = ac.seen;
                e.fields["seen"] = std::to_string(ll);
            }
            if (ac.has_seen_pos && to_ll(ac.seen_pos, ll)) {
                e.fields["seen_pos"] = std::to_string(ll);
            }
        });
    }
}

} // namespace adsb
