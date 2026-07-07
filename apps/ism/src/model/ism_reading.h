/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "entity_store.h"

#include <string>
#include <utility>
#include <vector>

namespace ism {

// One decoded ISM-band transmission as emitted by `rtl_433 -F json` (one JSON
// object per line). This header is the parsing CONTRACT — the python oracle in
// test/ mirrors it independently. See radio-apps/10-sigint-expansion.md (10b).
//
// Field mapping (JSON key -> member):
//   "model"          -> model (string, REQUIRED — a line without a non-empty
//                       string "model" is rejected by the parser)
//   "id"             -> id       (number or string -> canonical string; see below)
//   "channel"        -> channel  (number or string -> canonical string)
//   "type"           -> type     (string, e.g. "TPMS")
//   "time"           -> time     (string, verbatim)
//   "temperature_C"  -> has_temp / temperature_c   (number)
//   "humidity"       -> has_hum / humidity          (number)
//   "pressure_kPa"   -> has_pressure / pressure_kpa (number)
//   "pressure_PSI"   -> has_pressure / pressure_kpa (number, converted with
//                       kPa = PSI * 6.894757293168361; when BOTH keys are
//                       present, pressure_kPa wins)
//   "battery_ok"     -> has_batt / battery_ok       (number: nonzero -> true; or bool)
//   "rssi"           -> has_rssi / rssi              (number, dB)
//   "snr"            -> has_snr / snr                (number, dB)
//   "freq"           -> has_freq / freq_mhz          (number, MHz)
//   anything else    -> extras, as (key, canonical-string) pairs, SORTED BY KEY
//                       (deterministic; nlohmann object iteration is key-sorted).
//                       Only scalar values land in extras: null, arrays and
//                       nested objects are dropped.
//
// Canonical scalar stringification (shared by id/channel and extras; the
// oracle implements the same rules):
//   string  -> verbatim (UTF-8 bytes as decoded from the JSON escapes)
//   bool    -> "true" / "false"
//   number  -> canonicalized VIA ITS IEEE-754 double value (integer JSON
//              numbers are converted to double first, on both sides): if that
//              double is integral and |v| < 2^53, decimal integer text (no
//              sign for -0, no exponent); otherwise printf "%.6g" with C
//              ("C" locale) semantics.
//
// Parsing rules the oracle mirrors:
//   - duplicate JSON keys: LAST occurrence wins (both parsers behave so);
//   - non-standard JSON constants (NaN/Infinity/-Infinity) are invalid ->
//     the whole line is rejected;
//   - ordinary whitespace around a valid JSON object is accepted;
//   - extras are sorted by key BYTE-WISE on the UTF-8 encoding (identical to
//     codepoint order — a property of UTF-8).
//
// Non-string "model"/"type"/"time" values are treated as absent (empty).
// Mapped numeric fields accept only JSON numbers (a string "24.1" does NOT
// set temperature_C); battery_ok additionally accepts a JSON bool. A mapped
// key whose value has the wrong type is CONSUMED and dropped — it never
// falls through to extras (extras hold only keys outside the mapping).
struct IsmReading {
    std::string model;
    std::string id;       // empty -> not reported
    std::string channel;  // empty -> not reported
    std::string type;     // empty -> not reported
    std::string time;     // empty -> not reported

    bool has_temp{false};
    double temperature_c{0.0};
    bool has_hum{false};
    double humidity{0.0};
    bool has_pressure{false};
    double pressure_kpa{0.0};
    bool has_batt{false};
    bool battery_ok{false};
    bool has_rssi{false};
    double rssi{0.0};
    bool has_snr{false};
    double snr{0.0};
    bool has_freq{false};
    double freq_mhz{0.0};

    // Unmapped scalar fields, key-sorted. Values use the canonical
    // stringification above.
    std::vector<std::pair<std::string, std::string>> extras;

    // Stable per-device key for the EntityStore: "model", then "/id" when id
    // is non-empty, then "@channel" when channel is non-empty.
    // e.g. "Nexus-TH/155@1", "Schrader/7A2B1C", "Interlogix".
    std::string device_key() const;
};

// Parse ONE line of `rtl_433 -F json` output into `out`. Returns false (and
// leaves `out` unspecified) for: empty/whitespace-only lines, malformed JSON,
// a JSON value that is not an object, or a missing/empty/non-string "model".
// Never throws.
bool parse_ism_json_line(const std::string& line, IsmReading& out);

// Merge a reading into the generic EntityStore, keyed by device_key():
// mapped fields land as display strings (temp/hum/press/rssi/snr with one
// decimal, freq with three, battery as "ok"/"low"), extras as "x:<key>".
void apply_to_store(toolkit::EntityStore& store, const IsmReading& r);

} // namespace ism
