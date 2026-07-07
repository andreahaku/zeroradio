/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ism_reading.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>

namespace ism {
namespace {

using nlohmann::json;

constexpr double kPsiToKpa = 6.894757293168361;
constexpr double kTwo53 = 9007199254740992.0; // 2^53

// Canonical scalar stringification per the ism_reading.h contract. Returns
// false for non-scalars (null/array/object), which are dropped.
bool canon_scalar(const json& v, std::string& out) {
    if (v.is_string()) {
        out = v.get<std::string>();
        return true;
    }
    if (v.is_boolean()) {
        out = v.get<bool>() ? "true" : "false";
        return true;
    }
    if (v.is_number()) {
        const double d = v.get<double>();
        if (std::nearbyint(d) == d && std::fabs(d) < kTwo53) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
            out = buf;
        } else {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%.6g", d);
            out = buf;
        }
        return true;
    }
    return false;
}

// A mapped numeric field: JSON numbers only (bool is not a number in nlohmann).
bool get_num(const json& j, const char* key, double& out) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return false;
    out = it->get<double>();
    return true;
}

} // namespace

std::string IsmReading::device_key() const {
    std::string key = model;
    if (!id.empty()) key += "/" + id;
    if (!channel.empty()) key += "@" + channel;
    return key;
}

bool parse_ism_json_line(const std::string& line, IsmReading& out) {
    const json j = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return false;

    const auto model_it = j.find("model");
    if (model_it == j.end() || !model_it->is_string()) return false;
    out = IsmReading{};
    out.model = model_it->get<std::string>();
    if (out.model.empty()) return false;

    // id / channel: string or number -> canonical string.
    for (const auto& [key, dst] : {std::pair<const char*, std::string*>{"id", &out.id},
                                   {"channel", &out.channel}}) {
        const auto it = j.find(key);
        if (it != j.end() && (it->is_string() || it->is_number())) {
            canon_scalar(*it, *dst);
        }
    }
    // type / time: strings only, verbatim.
    for (const auto& [key, dst] : {std::pair<const char*, std::string*>{"type", &out.type},
                                   {"time", &out.time}}) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_string()) *dst = it->get<std::string>();
    }

    out.has_temp = get_num(j, "temperature_C", out.temperature_c);
    out.has_hum = get_num(j, "humidity", out.humidity);
    out.has_rssi = get_num(j, "rssi", out.rssi);
    out.has_snr = get_num(j, "snr", out.snr);
    out.has_freq = get_num(j, "freq", out.freq_mhz);

    // Pressure: kPa wins over PSI (converted) when both parse as numbers.
    double kpa = 0.0, psi = 0.0;
    if (get_num(j, "pressure_kPa", kpa)) {
        out.has_pressure = true;
        out.pressure_kpa = kpa;
    } else if (get_num(j, "pressure_PSI", psi)) {
        out.has_pressure = true;
        out.pressure_kpa = psi * kPsiToKpa;
    }

    // battery_ok: bool, or number (nonzero -> true).
    if (const auto it = j.find("battery_ok"); it != j.end()) {
        if (it->is_boolean()) {
            out.has_batt = true;
            out.battery_ok = it->get<bool>();
        } else if (it->is_number()) {
            out.has_batt = true;
            out.battery_ok = it->get<double>() != 0.0;
        }
    }

    // Everything else -> extras (key-sorted: nlohmann objects iterate sorted).
    static const char* const kMapped[] = {
        "model", "id", "channel", "type", "time", "temperature_C", "humidity",
        "pressure_kPa", "pressure_PSI", "battery_ok", "rssi", "snr", "freq",
    };
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool mapped = false;
        for (const char* m : kMapped) {
            if (it.key() == m) {
                mapped = true;
                break;
            }
        }
        if (mapped) continue;
        std::string value;
        if (canon_scalar(it.value(), value)) {
            out.extras.emplace_back(it.key(), std::move(value));
        }
    }
    return true;
}

void apply_to_store(toolkit::EntityStore& store, const IsmReading& r) {
    store.upsert(r.device_key(), [&r](toolkit::Entity& e) {
        // Sparse merge, same discipline as the ADS-B/AIS stores: only fields
        // this transmission carried are overwritten.
        e.fields["model"] = r.model;
        if (!r.id.empty()) e.fields["id"] = r.id;
        if (!r.channel.empty()) e.fields["channel"] = r.channel;
        if (!r.type.empty()) e.fields["type"] = r.type;
        if (!r.time.empty()) e.fields["time"] = r.time;

        char buf[32];
        if (r.has_temp) {
            std::snprintf(buf, sizeof(buf), "%.1f", r.temperature_c);
            e.fields["temp"] = buf;
        }
        if (r.has_hum) {
            std::snprintf(buf, sizeof(buf), "%.0f", r.humidity);
            e.fields["hum"] = buf;
        }
        if (r.has_pressure) {
            std::snprintf(buf, sizeof(buf), "%.1f", r.pressure_kpa);
            e.fields["press"] = buf;
        }
        if (r.has_batt) e.fields["batt"] = r.battery_ok ? "ok" : "low";
        if (r.has_rssi) {
            std::snprintf(buf, sizeof(buf), "%.1f", r.rssi);
            e.fields["rssi"] = buf;
        }
        if (r.has_snr) {
            std::snprintf(buf, sizeof(buf), "%.1f", r.snr);
            e.fields["snr"] = buf;
        }
        if (r.has_freq) {
            std::snprintf(buf, sizeof(buf), "%.3f", r.freq_mhz);
            e.fields["freq"] = buf;
        }
        for (const auto& [key, value] : r.extras) {
            e.fields["x:" + key] = value;
        }
    });
}

} // namespace ism
