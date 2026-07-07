/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 *
 * Frozen parity test for the rtl_433 JSON-line parser (apps/ism
 * src/model/ism_reading — parse_ism_json_line + device_key). Two-source
 * discipline: the expected values in ism_parse_vectors.inc are produced by an
 * independent python oracle (test/gen_ism_vectors.py) that implements the
 * contract documented in ism_reading.h from scratch (python json module vs
 * the nlohmann-based parser under test). The vector set covers every mapping
 * branch (synth) plus real transmissions captured off-air with an RTL-SDR
 * Blog V4 (TPMS from passing cars, 433.92 MHz, 2026-07-07).
 *
 * Deliberate scope note: apply_to_store (the EntityStore/UI projection) is NOT
 * part of this frozen reward — it is display formatting, verified by the app
 * render-proof. The frozen surface is the parse core + canonicalization +
 * device_key.
 *
 * This file is frozen (reward-guard): do not edit it to make code pass.
 */

#include "ism_reading.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_fails = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_fails;                                                         \
            std::printf("FAIL %s:%d  ", __FILE__, __LINE__);                   \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

struct Extra {
    const char* key;
    const char* value;
};

struct Vector {
    const char* raw;
    bool valid;
    const char* model;
    const char* id;
    const char* channel;
    const char* type;
    const char* time;
    bool has_temp;
    double temp;
    bool has_hum;
    double hum;
    bool has_press;
    double press;
    bool has_batt;
    bool batt;
    bool has_rssi;
    double rssi;
    bool has_snr;
    double snr;
    bool has_freq;
    double freq;
    std::vector<Extra> extras;
};

const std::vector<Vector> kVectors = {
#include "ism_parse_vectors.inc"
};

} // namespace

int main() {
    int idx = 0;
    for (const auto& v : kVectors) {
        ++idx;
        ism::IsmReading r;
        const bool ok = ism::parse_ism_json_line(v.raw, r);
        CHECK(ok == v.valid, "[%d] valid: got %d, expected %d (raw: %.60s)",
              idx, ok, v.valid, v.raw);
        if (!ok || !v.valid) continue;

        CHECK(r.model == v.model, "[%d] model: '%s' vs '%s'", idx, r.model.c_str(), v.model);
        CHECK(r.id == v.id, "[%d] id: '%s' vs '%s'", idx, r.id.c_str(), v.id);
        CHECK(r.channel == v.channel, "[%d] channel: '%s' vs '%s'", idx,
              r.channel.c_str(), v.channel);
        CHECK(r.type == v.type, "[%d] type: '%s' vs '%s'", idx, r.type.c_str(), v.type);
        CHECK(r.time == v.time, "[%d] time: '%s' vs '%s'", idx, r.time.c_str(), v.time);

        CHECK(r.has_temp == v.has_temp, "[%d] has_temp", idx);
        if (r.has_temp && v.has_temp)
            CHECK(r.temperature_c == v.temp, "[%d] temp: %.17g vs %.17g", idx,
                  r.temperature_c, v.temp);
        CHECK(r.has_hum == v.has_hum, "[%d] has_hum", idx);
        if (r.has_hum && v.has_hum)
            CHECK(r.humidity == v.hum, "[%d] hum: %.17g vs %.17g", idx, r.humidity, v.hum);
        CHECK(r.has_pressure == v.has_press, "[%d] has_pressure", idx);
        if (r.has_pressure && v.has_press)
            CHECK(r.pressure_kpa == v.press, "[%d] press: %.17g vs %.17g", idx,
                  r.pressure_kpa, v.press);
        CHECK(r.has_batt == v.has_batt, "[%d] has_batt", idx);
        if (r.has_batt && v.has_batt)
            CHECK(r.battery_ok == v.batt, "[%d] battery_ok: %d vs %d", idx,
                  r.battery_ok, v.batt);
        CHECK(r.has_rssi == v.has_rssi, "[%d] has_rssi", idx);
        if (r.has_rssi && v.has_rssi)
            CHECK(r.rssi == v.rssi, "[%d] rssi: %.17g vs %.17g", idx, r.rssi, v.rssi);
        CHECK(r.has_snr == v.has_snr, "[%d] has_snr", idx);
        if (r.has_snr && v.has_snr)
            CHECK(r.snr == v.snr, "[%d] snr: %.17g vs %.17g", idx, r.snr, v.snr);
        CHECK(r.has_freq == v.has_freq, "[%d] has_freq", idx);
        if (r.has_freq && v.has_freq)
            CHECK(r.freq_mhz == v.freq, "[%d] freq: %.17g vs %.17g", idx, r.freq_mhz, v.freq);

        CHECK(r.extras.size() == v.extras.size(), "[%d] extras count: %zu vs %zu", idx,
              r.extras.size(), v.extras.size());
        const size_t n = std::min(r.extras.size(), v.extras.size());
        for (size_t i = 0; i < n; ++i) {
            CHECK(r.extras[i].first == v.extras[i].key, "[%d] extras[%zu] key: '%s' vs '%s'",
                  idx, i, r.extras[i].first.c_str(), v.extras[i].key);
            CHECK(r.extras[i].second == v.extras[i].value,
                  "[%d] extras[%zu] value: '%s' vs '%s'", idx, i,
                  r.extras[i].second.c_str(), v.extras[i].value);
        }

        // device_key: derive the expectation from the ORACLE's fields (not the
        // parser's) per the documented rule, so a wrong id/channel cannot
        // launder itself into a "matching" key.
        std::string want_key = v.model;
        if (v.id[0] != '\0') want_key += std::string("/") + v.id;
        if (v.channel[0] != '\0') want_key += std::string("@") + v.channel;
        CHECK(r.device_key() == want_key, "[%d] device_key: '%s' vs '%s'", idx,
              r.device_key().c_str(), want_key.c_str());
    }

    if (g_fails == 0) {
        std::printf("ism_parse_test: all %zu vectors passed\n", kVectors.size());
        return 0;
    }
    std::printf("ism_parse_test: %d check(s) FAILED\n", g_fails);
    return 1;
}
