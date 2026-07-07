#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
# SPDX-License-Identifier: MIT
#
# Independent oracle for the rtl_433 JSON-line parser (apps/ism ism_reading).
# Implements the CONTRACT documented in src/model/ism_reading.h from scratch
# (python json module + the documented canonicalization rules), then emits the
# frozen C++ vectors ism_parse_vectors.inc. Two sources must agree: this
# script's expectations vs the nlohmann-based C++ parser under test.
#
# Usage: python3 gen_ism_vectors.py   (run from apps/ism/test/)
# Real captured lines are read from ism_capture.jsonl (one rtl_433 JSON line
# each) and appended as REAL vectors; synthetic cases cover the edge branches.

import json
import os

MAPPED = {
    "model", "id", "channel", "type", "time",
    "temperature_C", "humidity", "pressure_kPa", "pressure_PSI", "battery_ok",
    "rssi", "snr", "freq",
}

PSI_TO_KPA = 6.894757293168361

TWO_53 = 2.0 ** 53


def canon(v):
    """Canonical scalar stringification per the contract. None for non-scalars."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, str):
        return v
    if isinstance(v, (int, float)):
        f = float(v)
        if f.is_integer() and abs(f) < TWO_53:
            return str(int(f))
        return "%.6g" % f
    return None


def expect(line):
    """Oracle: parse one line per the contract. Returns None when rejected."""
    s = line.strip()
    if not s:
        return None

    def reject_constant(_):
        raise ValueError("non-standard JSON constant")

    try:
        obj = json.loads(s, parse_constant=reject_constant)
    except ValueError:
        return None
    if not isinstance(obj, dict):
        return None
    model = obj.get("model")
    if not isinstance(model, str) or model == "":
        return None

    r = {
        "model": model, "id": "", "channel": "", "type": "", "time": "",
        "has_temp": False, "temp": 0.0, "has_hum": False, "hum": 0.0,
        "has_press": False, "press": 0.0, "has_batt": False, "batt": False,
        "has_rssi": False, "rssi": 0.0, "has_snr": False, "snr": 0.0,
        "has_freq": False, "freq": 0.0, "extras": [],
    }

    def num(key):
        v = obj.get(key)
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            return None
        return float(v)

    v = obj.get("id")
    if isinstance(v, (str, int, float)) and not isinstance(v, bool):
        r["id"] = canon(v)
    v = obj.get("channel")
    if isinstance(v, (str, int, float)) and not isinstance(v, bool):
        r["channel"] = canon(v)
    for k, dst in (("type", "type"), ("time", "time")):
        v = obj.get(k)
        if isinstance(v, str):
            r[dst] = v

    for k, has, dst in (("temperature_C", "has_temp", "temp"),
                        ("humidity", "has_hum", "hum"),
                        ("rssi", "has_rssi", "rssi"),
                        ("snr", "has_snr", "snr"),
                        ("freq", "has_freq", "freq")):
        n = num(k)
        if n is not None:
            r[has] = True
            r[dst] = n

    kpa = num("pressure_kPa")
    psi = num("pressure_PSI")
    if kpa is not None:
        r["has_press"], r["press"] = True, kpa
    elif psi is not None:
        r["has_press"], r["press"] = True, psi * PSI_TO_KPA

    v = obj.get("battery_ok")
    if isinstance(v, bool):
        r["has_batt"], r["batt"] = True, v
    elif isinstance(v, (int, float)):
        r["has_batt"], r["batt"] = True, (float(v) != 0.0)

    for k in sorted(obj.keys()):
        if k in MAPPED:
            continue
        c = canon(obj[k])
        if c is not None:
            r["extras"].append((k, c))
    return r


def key_of(r):
    key = r["model"]
    if r["id"]:
        key += "/" + r["id"]
    if r["channel"]:
        key += "@" + r["channel"]
    return key


SYNTH = [
    # (name, raw line) — one branch per case, mirrored in the test comments.
    ("weather_full",
     '{"time" : "2026-07-07 16:20:00", "model" : "Nexus-TH", "id" : 155, '
     '"channel" : 1, "battery_ok" : 1, "temperature_C" : 24.100, '
     '"humidity" : 40, "mic" : "CRC", "protocol" : 19}'),
    ("tpms_string_id",
     '{"model":"Schrader","type":"TPMS","id":"7A2B1C","pressure_kPa":220.0,'
     '"temperature_C":25.0,"flags":"08"}'),
    ("tpms_psi_conversion",
     '{"model":"Toyota","type":"TPMS","id":"d75e2810","pressure_PSI":39.25,'
     '"status":128}'),
    ("pressure_kpa_wins_over_psi",
     '{"model":"DualPress","pressure_kPa":250.0,"pressure_PSI":39.25}'),
    ("string_id_preserved_channel_number",
     '{"model":"X10-RF","id":"0042","channel":2.0,"event":"ON"}'),
    ("level_metadata_bool_battery",
     '{"model":"Acurite-Tower","id":1234,"battery_ok":true,"rssi":-8.2,'
     '"snr":15.3,"noise":-23.5,"freq":433.92882}'),
    ("minimal_model_only",
     '{"model":"Interlogix"}'),
    ("canonicalization_extremes",
     '{"model":"Edge","wind_avg_km_h":3.6,"big_int":1234567890,'
     '"big_non53":9007199254740992.0,"neg_zero":-0.0,"neg":-12.5}'),
    ("numeric_boundaries",
     '{"model":"Bounds","just_below_53":9007199254740991,'
     '"at_53":9007199254740992,"uint64_max":18446744073709551615,'
     '"exp_integral":1.5e3,"exp_tiny":2.5e-4,"extra_zero":0,'
     '"extra_false":false,"extra_empty":""}'),
    ("id_channel_numeric_shapes",
     '{"model":"NumKey","id":-0.0,"channel":9007199254740991}'),
    ("battery_zero_and_false",
     '{"model":"BattA","battery_ok":0}'),
    ("battery_bool_false",
     '{"model":"BattB","battery_ok":false}'),
    ("pressure_psi_wrong_type_kpa_valid",
     '{"model":"PressMix","pressure_PSI":"39","pressure_kPa":250.0}'),
    ("pressure_kpa_wrong_type_psi_valid",
     '{"model":"PressMix2","pressure_kPa":"250","pressure_PSI":39.25}'),
    ("mapped_null_and_array_consumed",
     '{"model":"OddTypes","temperature_C":null,"humidity":[1],"snr":true}'),
    ("duplicate_keys_last_wins",
     '{"model":"First","model":"Second","temperature_C":1.0,"temperature_C":2.0}'),
    ("whitespace_around_object",
     '   {"model":"Padded","id":7}   '),
    ("unicode_key_and_escapes",
     '{"model":"Uni","caf\\u00e8_key":"v","esc":"a\\\\b\\/c\\nd"}'),
    ("non_scalars_dropped",
     '{"model":"Nested","ok":1,"obj":{"a":1},"arr":[1,2],"nul":null}'),
    ("mapped_type_mismatch_consumed",
     '{"model":"Odd","temperature_C":"24.1","battery_ok":"yes","id":true}'),
    ("unicode_passthrough",
     '{"model":"Acme-Sensor","note":"caff\\u00e8 \\"quoted\\"","time":"2026-07-07 16:21:07"}'),
]

NEGATIVE = [
    ("empty", ""),
    ("whitespace", "   "),
    ("malformed", '{"model":"broken"'),
    ("json_array", "[1,2,3]"),
    ("json_scalar", "42"),
    ("missing_model", '{"id":5,"temperature_C":21.0}'),
    ("empty_model", '{"model":"","id":5}'),
    ("non_string_model", '{"model":433,"id":5}'),
    ("nan_constant_rejected", '{"model":"A","temperature_C":NaN}'),
    ("infinity_rejected", '{"model":"A","snr":Infinity}'),
]


def cpp_str(s):
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def cpp_dbl(f):
    return "%.17g" % f


def emit(fp, name, source, raw, r):
    fp.write("// %s (%s)\n" % (name, source))
    if r is None:
        fp.write("{ %s, false, \"\", \"\", \"\", \"\", \"\", "
                 "false, 0, false, 0, false, 0, false, false, "
                 "false, 0, false, 0, false, 0, {} },\n" % cpp_str(raw))
        return
    extras = ", ".join("{%s, %s}" % (cpp_str(k), cpp_str(v)) for k, v in r["extras"])
    fp.write("{ %s, true, %s, %s, %s, %s, %s, "
             "%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, {%s} },\n" % (
                 cpp_str(raw), cpp_str(r["model"]), cpp_str(r["id"]),
                 cpp_str(r["channel"]), cpp_str(r["type"]), cpp_str(r["time"]),
                 "true" if r["has_temp"] else "false", cpp_dbl(r["temp"]),
                 "true" if r["has_hum"] else "false", cpp_dbl(r["hum"]),
                 "true" if r["has_press"] else "false", cpp_dbl(r["press"]),
                 "true" if r["has_batt"] else "false", "true" if r["batt"] else "false",
                 "true" if r["has_rssi"] else "false", cpp_dbl(r["rssi"]),
                 "true" if r["has_snr"] else "false", cpp_dbl(r["snr"]),
                 "true" if r["has_freq"] else "false", cpp_dbl(r["freq"]),
                 extras))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(here, "ism_parse_vectors.inc")
    cap_path = os.path.join(here, "ism_capture.jsonl")

    with open(out_path, "w") as fp:
        fp.write("// GENERATED by gen_ism_vectors.py — DO NOT EDIT BY HAND.\n")
        fp.write("// Frozen oracle vectors for parse_ism_json_line (see ism_reading.h).\n")
        fp.write("// Row: raw, valid, model, id, channel, type, time,\n")
        fp.write("//      has_temp, temp, has_hum, hum, has_press, press, has_batt, batt,\n")
        fp.write("//      has_rssi, rssi, has_snr, snr, has_freq, freq, extras{key,value}\n")
        for name, raw in SYNTH:
            emit(fp, name, "SYNTH", raw, expect(raw))
        for name, raw in NEGATIVE:
            emit(fp, name, "SYNTH-NEGATIVE", raw, expect(raw))

        n_real = 0
        if os.path.exists(cap_path):
            seen_models = set()
            for line in open(cap_path):
                line = line.strip()
                r = expect(line)
                if r is None:
                    continue
                if r["model"] in seen_models:
                    continue
                seen_models.add(r["model"])
                n_real += 1
                emit(fp, "real_%s" % r["model"].replace(" ", "_"), "CAPTURED (RTL-SDR V4)", line, r)
                if n_real >= 4:
                    break
        if n_real == 0:
            fp.write("// WARNING: no CAPTURED vectors — ism_capture.jsonl absent or empty.\n")
        print("wrote %s (%d synth + %d negative + %d real)" %
              (out_path, len(SYNTH), len(NEGATIVE), n_real))

    # Also print the device keys for the synth positives (documentation aid).
    for name, raw in SYNTH:
        r = expect(raw)
        if r:
            print("  key(%s) = %s" % (name, key_of(r)))


if __name__ == "__main__":
    main()
