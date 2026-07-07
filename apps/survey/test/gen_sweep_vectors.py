#!/usr/bin/env python3
"""Generate sweep_parser_vectors.inc (frozen parity oracle for SweepAccumulator).

Two-source oracle: this script implements the parsing contract documented in
../src/sweep/sweep_accumulator.h INDEPENDENTLY in python and derives every
expectation (sweep counts, resampled spot values, exact peak lists) from it.
The C++ implementation under test must agree.

Run from this directory:  python3 gen_sweep_vectors.py
Provenance: SYNTH = fabricated edge case; CAPTURED = real rtl_power stdout
from the RTL-SDR V4 (drop the capture at fm_capture.csv beside this script).
"""
import math
import os
import statistics

NO_DATA = -150.0

# ------------------------------------------------------------------ oracle

def _num(field):
    """Full-token finite parse; None when invalid (contract: full-token only)."""
    try:
        v = float(field)
    except ValueError:
        return None
    return v if math.isfinite(v) else None

def parse_line(line):
    """One line -> (hz_low, hz_step, [dbm...]) or None if malformed/blank."""
    line = line.rstrip("\r").strip()
    if not line:
        return None
    fields = [f.strip() for f in line.split(",")]
    if len(fields) < 7:
        return None
    hz_low, hz_high, hz_step = _num(fields[2]), _num(fields[3]), _num(fields[4])
    if hz_low is None or hz_high is None or hz_step is None:
        return None
    if not (hz_low >= 0 and hz_step > 0 and hz_low < hz_high):
        return None
    bins = []
    for f in fields[6:]:
        v = _num(f)
        bins.append(v if v is not None else NO_DATA)
    return (hz_low, hz_step, bins)

class Oracle:
    """Mirrors SweepAccumulator: feed complete lines, flush handles the
    trailing partial line then publishes pending points."""

    def __init__(self, start_hz, stop_hz):
        self.start = start_hz
        self.stop = stop_hz
        self.pending = {}
        self.frame = None
        self.sweeps = 0
        self.prev_low = None

    def _row(self, row):
        hz_low, hz_step, bins = row
        done = 0
        if self.prev_low is not None and hz_low < self.prev_low and self.pending:
            self._publish()
            done = 1
        self.prev_low = hz_low
        for i, dbm in enumerate(bins):
            centre = hz_low + hz_step * i + hz_step / 2.0
            if self.start <= centre < self.stop:
                self.pending[centre] = dbm
        return done

    def run(self, text, do_flush):
        """feed(text) then optionally flush(). Returns sweeps completed."""
        parts = text.split("\n")
        complete, partial = parts[:-1], parts[-1]
        done = 0
        for line in complete:
            row = parse_line(line)
            if row:
                done += self._row(row)
        if do_flush:
            row = parse_line(partial)
            if row:
                done += self._row(row)
            if self.pending:
                self._publish()
                done += 1
        return done

    def _publish(self):
        self.frame = sorted(self.pending.items())
        self.pending = {}
        self.sweeps += 1

    def frame_dbm(self, n_bins):
        assert self.frame is not None
        width = (self.stop - self.start) / n_bins
        out = []
        for k in range(n_bins):
            lo = self.start + k * width
            hi = lo + width
            cell = [d for c, d in self.frame if lo <= c < hi]
            out.append(sum(cell) / len(cell) if cell else NO_DATA)
        return out

    def peaks(self, threshold_db, max_peaks=16):
        assert self.frame is not None
        d = [dbm for _, dbm in self.frame]
        med = statistics.median(d)
        found = []
        for i in range(1, len(d) - 1):
            if d[i] > d[i - 1] and d[i] >= d[i + 1] and d[i] >= med + threshold_db:
                found.append((self.frame[i][0], d[i]))
        found.sort(key=lambda p: (-p[1], p[0]))
        return found[:max_peaks]

# ------------------------------------------------------------- synth rows

def row(hz_low, hz_high, hz_step, bins, date="2026-07-07", time="10:00:00"):
    return "%s, %s, %d, %d, %d, 32, %s" % (
        date, time, hz_low, hz_high, hz_step, ", ".join(str(b) for b in bins))

FLOOR = -80.0

def seg(hz_low, n, step, hot=None):
    bins = [FLOOR + (i % 3) for i in range(n)]  # -80,-79,-78 pattern (deterministic)
    if hot is not None:
        bins[hot[0]] = hot[1]
    return row(hz_low, hz_low + n * step, step, bins)

# (name, prov, csv, chunk, do_flush, start, stop, threshold, n_probe_bins, probe_idx)
VECTORS = []

# 1. three segments + carrier, wrap publishes sweep 1 (second sweep pending, NOT flushed)
csv1 = "\n".join([
    seg(100_000_000, 4, 100_000),
    seg(100_400_000, 4, 100_000, hot=(2, -30.0)),   # carrier @ 100.65 MHz
    seg(100_800_000, 4, 100_000),                    # last 2 bins beyond stop -> dropped
    seg(100_000_000, 4, 100_000),                    # wrap -> publish
]) + "\n"
VECTORS.append(("rtl_wrap_carrier", "SYNTH", csv1, 0, False,
                100_000_000, 101_000_000, 5.0, 10, [0, 3, 6, 9]))

# 2. same segments, no wrap row, flush() publishes
csv2 = "\n".join([
    seg(100_000_000, 4, 100_000),
    seg(100_400_000, 4, 100_000, hot=(2, -30.0)),
    seg(100_800_000, 4, 100_000),
]) + "\n"
VECTORS.append(("oneshot_flush", "SYNTH", csv2, 0, True,
                100_000_000, 101_000_000, 5.0, 10, [0, 6]))

# 3. hackrf-style: wide integer bins over 2.4 GHz, wrap
csv3 = "\n".join([
    row(2_400_000_000, 2_420_000_000, 1_000_000, [FLOOR] * 10 + [-42.5] + [FLOOR] * 9),
    row(2_420_000_000, 2_440_000_000, 1_000_000, [FLOOR] * 20),
    row(2_400_000_000, 2_420_000_000, 1_000_000, [FLOOR] * 20),  # wrap
]) + "\n"
VECTORS.append(("hackrf_wide", "SYNTH", csv3, 0, False,
                2_400_000_000, 2_440_000_000, 5.0, 8, [0, 2, 7]))

# 4. malformed rows are skipped whole; valid rows still stitch
csv4 = "\n".join([
    seg(100_000_000, 4, 100_000),
    "hello,world",
    "2026-07-07, 10:00:01, 100400000, 100800000",          # <7 fields
    "2026-07-07, 10:00:01, zzz, 100800000, 100000, 32, -70, -70, -70, -70",
    "2026-07-07, 10:00:01, 100000000abc, 100800000, 100000, 32, -70, -70, -70, -70",
    "2026-07-07, 10:00:01, inf, 100800000, 100000, 32, -70, -70, -70, -70",
    "2026-07-07, 10:00:01, -100400000, 100800000, 100000, 32, -70, -70, -70, -70",
    "2026-07-07, 10:00:01, 100800000, 100400000, 100000, 32, -70, -70, -70, -70",  # low>high
    "2026-07-07, 10:00:01, 100400000, 100800000, 0, 32, -70, -70, -70, -70",       # step 0
    seg(100_400_000, 4, 100_000, hot=(1, -25.0)),
    seg(100_000_000, 4, 100_000),                          # wrap
]) + "\n"
VECTORS.append(("malformed_skipped", "SYNTH", csv4, 0, False,
                100_000_000, 101_000_000, 5.0, 8, [1, 4]))

# 5. non-finite / unparsable dB fields -> kNoDataDbm for those bins only
csv5 = "\n".join([
    row(100_400_000, 100_800_000, 100_000, ["-inf", "nan", -60.0, "-61.5abc"]),
    seg(100_000_000, 4, 100_000),                          # wrap (lower hz_low)
]) + "\n"
VECTORS.append(("nonfinite_bins", "SYNTH", csv5, 0, False,
                100_000_000, 101_000_000, 3.0, 10, [4, 5, 6, 7]))

# 6. byte-chunked feed must equal single feed (chunk = 7 bytes)
VECTORS.append(("chunked_feed", "SYNTH", csv1, 7, False,
                100_000_000, 101_000_000, 5.0, 10, [3, 6]))

# 7. two wraps in ONE feed: sweeps()==2 and only the LATEST frame is readable
csv7 = "\n".join([
    seg(100_000_000, 4, 100_000),
    seg(100_400_000, 4, 100_000, hot=(1, -40.0)),
    seg(100_000_000, 4, 100_000),                          # wrap #1
    seg(100_400_000, 4, 100_000, hot=(1, -35.0)),
    seg(100_000_000, 4, 100_000),                          # wrap #2 (pending after)
]) + "\n"
VECTORS.append(("two_sweeps_latest", "SYNTH", csv7, 0, False,
                100_000_000, 101_000_000, 5.0, 10, [4, 5]))

# 8. duplicate centres in the same sweep: last row wins (equal hz_low != wrap)
csv8 = "\n".join([
    seg(100_000_000, 4, 100_000, hot=(1, -50.0)),
    seg(100_000_000, 4, 100_000, hot=(1, -28.0)),          # same hz_low: overwrite
    "2026-07-07, 10:00:02, 99000000, 99400000, 100000, 32, -80, -80, -80, -80",  # wrap (dropped: < start)
]) + "\n"
VECTORS.append(("dup_centre_lastwins", "SYNTH", csv8, 0, False,
                100_000_000, 101_000_000, 5.0, 4, [0, 1]))

# 9. rich peaks: edge maximum excluded, plateau counted once, dBm-desc order
csv9 = "\n".join([
    row(100_000_000, 100_800_000, 100_000,
        [-10.0, -20.0, FLOOR, -35.0, -35.0, FLOOR, -50.0, FLOOR]),
    seg(100_000_000, 4, 100_000),                          # wait: same hz_low -> NOT wrap
]) + "\n"
# fix: use a lower wrap row so the rich row publishes
csv9 = "\n".join([
    row(100_100_000, 100_900_000, 100_000,
        [-10.0, -20.0, FLOOR, -35.0, -35.0, FLOOR, -50.0, FLOOR]),
    seg(100_000_000, 1, 100_000),                          # wrap (lower hz_low)
]) + "\n"
VECTORS.append(("peaks_rich", "SYNTH", csv9, 0, False,
                100_000_000, 101_000_000, 5.0, 8, [1, 3]))

# 10. threshold equality: dbm == median + t is INCLUDED; just-below excluded
csv10 = "\n".join([
    row(100_100_000, 100_800_000, 100_000,
        [FLOOR, FLOOR, -60.0, FLOOR, -60.5, FLOOR, FLOOR]),
    seg(100_000_000, 1, 100_000),                          # wrap
]) + "\n"
VECTORS.append(("threshold_equality", "SYNTH", csv10, 0, False,
                100_000_000, 101_000_000, 20.0, 7, [2]))

# 11. sparse coverage: cells no segment covered resample to kNoDataDbm
csv11 = "\n".join([
    seg(100_000_000, 4, 100_000),
    seg(109_000_000, 4, 100_000, hot=(0, -44.0)),
    seg(100_000_000, 4, 100_000),                          # wrap
]) + "\n"
VECTORS.append(("sparse_cells", "SYNTH", csv11, 0, False,
                100_000_000, 110_000_000, 5.0, 10, [0, 4, 9]))

# 12. CRLF line endings + unterminated final line consumed by flush()
csv12 = ("\r\n".join([
    seg(100_000_000, 4, 100_000),
    seg(100_400_000, 4, 100_000, hot=(1, -33.0)),
]) + "\r\n" + seg(100_800_000, 2, 100_000))               # final line: NO newline
VECTORS.append(("crlf_eof_flush", "SYNTH", csv12, 0, True,
                100_000_000, 101_000_000, 5.0, 10, [4, 8]))

# 13. CAPTURED real rtl_power FM broadcast sweep (one-shot -1: flush publishes)
FM = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fm_capture.csv")
if os.path.exists(FM):
    fm_csv = open(FM).read()
    VECTORS.append(("real_fm_broadcast", "CAPTURED", fm_csv, 0, True,
                    88_000_000, 108_000_000, 12.0, 320, [0, 100, 200, 319]))
else:
    print("NOTE: fm_capture.csv not found — real FM vector NOT emitted yet")

# ---------------------------------------------------------------- emission

def cstr(text):
    out = []
    for line in text.split("\n"):
        esc = line.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "\\r")
        out.append('"' + esc + '\\n"')
    if text and not text.endswith("\n"):
        out[-1] = out[-1][:-3] + '"'  # drop the trailing \n escape on a partial line
    elif out and out[-1] == '"\\n"':
        out.pop()
    return "\n    ".join(out)

def fnum(v):
    return repr(float(v)) + "f"

chunks = []
tables = []
for name, prov, csv, chunk, do_flush, start, stop, thr, n_probe, idxs in VECTORS:
    o = Oracle(start, stop)
    o.run(csv, do_flush)
    frame = o.frame_dbm(n_probe)
    spots = ", ".join("{%d, %d, %s}" % (n_probe, i, fnum(frame[i])) for i in idxs)
    pk = o.peaks(thr)
    pks = ", ".join("{%dll, %s}" % (round(f), fnum(d)) for f, d in pk)
    chunks.append("static const char CSV_%s[] =\n    %s;" % (name, cstr(csv)))
    chunks.append("static const SpotExpect SPOTS_%s[] = {%s};" % (name, spots))
    if pk:
        chunks.append("static const PeakExpect PEAKS_%s[] = {%s};" % (name, pks))
    tables.append(
        '  {"%s", "%s", CSV_%s, sizeof(CSV_%s) - 1, %d, %s, %d, %s, %dll, %dll, %s,\n'
        "   SPOTS_%s, %d, %s, %d}," % (
            name, prov, name, name, chunk, "true" if do_flush else "false",
            o.sweeps, "true" if o.frame is not None else "false",
            start, stop, fnum(thr),
            name, len(idxs), ("PEAKS_" + name) if pk else "nullptr", len(pk)))

header = """\
/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 *
 * AUTO-GENERATED by gen_sweep_vectors.py (this directory) — FROZEN parity
 * oracle, do not edit. Two-source oracle: the python generator implements the
 * parsing contract of sweep_accumulator.h independently; the C++
 * SweepAccumulator must reproduce every expectation. Provenance per row:
 * SYNTH = fabricated edge case; CAPTURED = real rtl_power stdout (RTL-SDR V4).
 */

struct SpotExpect { int n_bins; int idx; float dbm; };
struct PeakExpect { long long freq_hz; float dbm; };
struct SweepVec {
  const char* name; const char* prov;
  const char* csv; unsigned long csv_len;
  int chunk;            // 0 = single feed(); k = feed k bytes at a time
  bool do_flush;        // call flush() after feeding
  int exp_sweeps;
  bool exp_frame;
  long long start_hz, stop_hz;
  float peak_threshold;
  const SpotExpect* spots; unsigned long n_spots;
  const PeakExpect* peaks; unsigned long n_peaks; // exact list, dBm desc, ties by freq
};
"""

out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sweep_parser_vectors.inc")
with open(out_path, "w") as f:
    f.write(header + "\n")
    f.write("\n\n".join(chunks))
    f.write("\n\nstatic const SweepVec kSweepVectors[] = {\n" + "\n".join(tables) + "\n};\n")
print("wrote %s with %d vectors" % (out_path, len(VECTORS)))
