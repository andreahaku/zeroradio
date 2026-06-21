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

// Map the ADS-B emitter category code (e.g. "A3") to a coarse class.
std::string category_from_emitter(const std::string& code) {
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

// Read a numeric JSON value as double, tolerating numbers and numeric strings.
bool read_number(const json& node, const char* key, double& out) {
    auto it = node.find(key);
    if (it == node.end()) {
        return false;
    }
    if (it->is_number()) {
        out = it->get<double>();
        return true;
    }
    if (it->is_string()) {
        const std::string s = it->get<std::string>();
        try {
            size_t consumed = 0;
            const double v = std::stod(s, &consumed);
            if (consumed > 0) {
                out = v;
                return true;
            }
        } catch (...) {
            return false;
        }
    }
    return false;
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

        double track = 0.0;
        if (read_number(node, "track", track)) {
            ac.has_track = true;
            ac.track = std::fmod(std::fmod(track, 360.0) + 360.0, 360.0);
        }

        // alt_baro may be a number or the string "ground".
        auto alt_it = node.find("alt_baro");
        if (alt_it != node.end()) {
            if (alt_it->is_number()) {
                ac.has_alt = true;
                ac.alt_baro = alt_it->get<double>();
            }
            // "ground" (or any non-numeric) -> has_alt stays false.
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
            ac.seen = seen;
        }

        ac.category = category_from_emitter(trim(read_string(node, "category")));

        result.push_back(std::move(ac));
    }

    return result;
}

void apply_to_store(toolkit::EntityStore& store, const std::vector<Aircraft>& aircraft) {
    for (const auto& ac : aircraft) {
        store.upsert(ac.hex, [&ac](toolkit::Entity& e) {
            if (ac.has_pos) {
                e.has_pos = true;
                e.pos.lat = ac.lat;
                e.pos.lon = ac.lon;
            }
            e.seen = ac.seen;

            // Merge string fields (only overwrite the ones we actually have, so
            // sparse updates accumulate like real ADS-B message types do).
            if (!ac.flight.empty()) {
                e.fields["flight"] = ac.flight;
            }
            if (ac.has_alt) {
                e.fields["alt"] = std::to_string(static_cast<long long>(ac.alt_baro));
            }
            if (ac.has_gs) {
                e.fields["gs"] = std::to_string(static_cast<long long>(ac.gs));
            }
            if (ac.has_track) {
                e.fields["track"] = std::to_string(static_cast<long long>(ac.track));
            }
            if (!ac.squawk.empty()) {
                e.fields["squawk"] = ac.squawk;
            }
            e.fields["category"] = ac.category;
            e.fields["emergency"] = ac.emergency ? "1" : "0";
            e.fields["seen"] = std::to_string(static_cast<long long>(ac.seen));
        });
    }
}

} // namespace adsb
