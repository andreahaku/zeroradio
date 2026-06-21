# ADS-B app — multi-model code review (synthesis)

Date: 2026-06-21 · Reviewers: Claude (Opus 4.8), Codex (GPT-5.4, xhigh), Gemini
(3.x, deep), kimi-k2.7-code (Ollama Cloud). Scope: `apps/adsb/**` + relevant
`toolkit/**`. The build is green; no automated tests exist for this app.

Each finding below was cross-checked against the actual source. **Convergence**
notes how many independent models raised it (higher = higher confidence).
Findings I verified as false/non-issues are listed at the end so they are not
re-investigated.

## Status (applied 2026-06-21, build green)

**Fixed:** H1, H2/H3 (minimal — via `seen_pos`, drop stale position only), M1,
M2, M3 (partial — trails-in-place + table cell diffing), M4, M5, L1, L2, L3, L4,
L5, L6, L8, L9, L12.

**Deferred (deliberate):** M3 row caching with a store generation counter (more
invasive, touches `EntityStore`; marginal gain at tens of aircraft); L7 (debounce
save — behavioural change); L10 (right-list marker — cosmetic UI).

**H2/H3 note:** only the *position* freshness was addressed (radar no longer
plots an aircraft at a stale spot once dump1090's `seen_pos` exceeds the TTL).
The TTL-vs-`seen` stamping and squawk/emergency clearing were left as-is per the
chosen minimal scope.

## Verdict

Solid, careful codebase. The raster helpers are all bounds-checked, every
`snprintf` is size-bounded (truncation, never overflow), the parser rejects
non-finite numbers, and the `EntityStore` single-mutex model is sound (`ok_` is
`std::atomic<bool>`, `upsert` runs its callback synchronously under the lock).
No memory-safety Critical. The real issues are: one UB corner case, two
correctness/semantics bugs (stale sparse-merge, dead `ADSB_TTL`), settings
input not validated, and per-tick allocation/redraw churn that matters on the
embedded target.

---

## High

### H1 — `to_ll` range guard permits UB at exactly 2^63
`apps/adsb/src/model/aircraft.cpp:86-94` · Convergence: **2** (Gemini Critical, Codex High) · **Verified**

`static_cast<double>(LLONG_MAX)` rounds up to `2^63`. A JSON value of exactly
`9223372036854775808.0` passes `v > (double)LLONG_MAX` (since `2^63 > 2^63` is
false) and is then cast back to `long long`, which is out of range → UB.
Practically unreachable from dump1090, but it is genuine UB.

**Fix:** reject the half-open range explicitly. `LLONG_MIN` is an exact power of
two as a double, so:
```cpp
if (!std::isfinite(v) ||
    v <  static_cast<double>(std::numeric_limits<long long>::min()) ||
    v >= -static_cast<double>(std::numeric_limits<long long>::min())) { // == 2^63
    return false;
}
```

### H2 — Sparse-merge retains stale state (position, emergency) indefinitely
`apps/adsb/src/model/aircraft.cpp:208-260` · Convergence: **1** (Codex; I flagged it too) · **Verified (partly by design)**

`apply_to_store` only overwrites fields the latest message carried; absent
fields keep their last value until the TTL sweep drops the whole aircraft. The
header comment documents this intentionally, but two cases are problematic:
- **stale position**: an aircraft that stops reporting lat/lon keeps plotting at
  its last spot on the radar until TTL expiry;
- **stale emergency**: an old 7500/7600/7700 squawk persists if later messages
  omit `squawk` (emergency only clears when a *new* squawk arrives).

Note: dump1090's `aircraft.json` is a full per-aircraft snapshot, not a sparse
event stream, so the sparse-merge premise is itself debatable here.

**Fix:** decide per-field freshness. At minimum, parse `seen_pos` and drop
`has_pos` when position is stale; consider clearing `squawk`/`emergency` when a
snapshot omits them. (Couples with H3.)

### H3 — App TTL ignores dump1090's reported `seen`; every poll resets `updated`
`toolkit/src/model/entity_store.cpp:22`, `apps/adsb/src/view/adsb_screen.cpp:920` · Convergence: **1** (Codex) · **Verified**

`upsert` stamps `updated = now` on every poll. So an aircraft that dump1090
still lists but with `seen` already > our TTL is never removed by our sweep — the
15/30 s TTL effectively can't fire while the record stays in the file. `seen` is
stored for display only.

**Fix:** make age effective: stamp `updated = now - seen` in a batch upsert, or
compute age as `reported_seen + elapsed_since_poll` in the sweep.

---

## Medium

### M1 — `ADSB_TTL` env override is parsed but never used
`apps/adsb/src/main.cpp:40-43` → `config.ttl_seconds` · Convergence: **2** (kimi, Codex) · **Verified**

`main` writes `config.ttl_seconds`, but `tick()` sweeps with
`vm_.ttl_seconds()` (from the persisted settings, default 30 s). `config.ttl_seconds`
is never read for the sweep. The documented env override is dead.

**Fix:** add `AdsbViewModel::set_ttl_seconds()` and apply the env value after
construction (define env-vs-persisted precedence), or seed the VM from `config`.

### M2 — Settings loaded without validation; partial reads silently applied
`apps/adsb/src/viewmodel/adsb_viewmodel.cpp:245-271` · Convergence: **2** (kimi, Codex) · **Verified**

`load_settings()` doesn't check the `>>` chain succeeded, accepts any `ttl > 0`
and any `trail >= 0`. A corrupt file can set huge TTL (expiry effectively off)
or a huge trail cap (memory growth — `trail_len` is used directly as the deque
cap, bypassing the intended 600-point safety). A truncated file applies whatever
parsed and leaves the rest at inline defaults.

**Fix:** parse into temporaries, require all fields (`if (!in) return;`),
validate against allowlists (`TTL∈{15,30,60,120}`, `trails∈{0,15,30,60}`,
`range 0..5`, bools 0/1), then commit. Save via temp-file + rename for atomicity.

### M3 — Per-tick allocation & redraw churn (300 ms tick, 2000 ms data)
`adsb_screen.cpp:435 (build_rows)`, `:576/609 (update_list)`, `:624-641 (record_trails)`, `entity_store.cpp:25 (snapshot)` · Convergence: **3** (kimi, Gemini, Codex) · **Verified**

Highest-convergence finding. Every 300 ms the app:
1. deep-copies the whole store incl. nested `map<string,string>` (`snapshot`),
   then copies again into `Row`s;
2. rebuilds + sorts all rows and re-parses strings;
3. rewrites **every** table cell via `lv_table_set_cell_value` (each call frees+
   reallocs the cell string and dirties layout — even when unchanged);
4. reconstructs the entire `trails_` map with `swap`.
Plus radar trails are `O(aircraft × trail_len)`.

**Fixes (incremental, by leverage):**
- **Trails in place**: erase vanished keys + append to existing deques instead of
  rebuilding the map each tick.
- **Diff table cells**: compare with `lv_table_get_cell_value` and only set on
  change.
- **Cache rows**: add a store generation counter; rebuild rows only when data/
  sort/settings change (the data only moves every 2 s).
- (Larger) typed entity fields to kill repeated string conversions; a
  `read_all(F)` / `apply_batch` accessor to build `Row`s under the lock without
  the intermediate deep copy.

### M4 — `record_trails` runs on the *filtered* row set → toggling a filter drops history
`adsb_screen.cpp:920-922` + `:468-479` · Convergence: **1** (kimi) · **Verified**

`tick()` passes the post-filter (`show_ground`, `emergency_only`) rows to
`record_trails`, so hidden aircraft lose their accumulated trail.

**Fix:** record trails from the unfiltered snapshot (split build into
build-all → record-trails → filter+sort).

### M5 — Reader-thread exceptions escape `run()` → `std::terminate`
`toolkit/src/net/file_json_source.cpp:52,68` · Convergence: **1** (Codex) · **Verified (latent)**

`parse_aircraft_json` uses `allow_exceptions=false` and the apply path doesn't
throw today, but any `std::bad_alloc` (whole file + JSON DOM read unbounded) or a
future throwing callback escaping `run()` calls `std::terminate`.

**Fix:** cap file size, and `try/catch(...)` at the thread boundary in `run()` —
mark the source unhealthy and continue rather than crash.

---

## Low

- **L1** Haversine `a` not clamped → near-antipodal rounding can give `a>1` →
  `sqrt(1-a)`=NaN → NaN range, which breaks the sort comparator's strict-weak
  ordering and reaches `lround(NaN)`. `geo.cpp:39`. Clamp `a` to `[0,1]`.
  (Codex; defensive.)
- **L2** Buffer truncation for 64-bit values: `alt_buf[12]`/`gs_buf[12]`/
  `rng_buf[12]` (`adsb_screen.cpp:591`) and `line[56]` recolor string
  (`:759`, dropping the closing `#` would bleed LVGL recolor). Safe from
  overflow, but corrupt/huge values display wrong or break layout. Bump to
  `[24]`/`[128]`. (kimi+Gemini convergence 2; defensive — real flight≤8 chars.)
- **L3** `has_seen` is computed then discarded; `Row` has no `has_seen`, so
  Detail shows `0s` for missing `seen`. `adsb_screen.cpp:453-454,824`. Keep
  `has_seen` in `Row`, show `-`. (Codex.)
- **L4** `ok_` latches green forever: a file that later disappears, or readable
  malformed JSON, still reads healthy. `file_json_source.cpp:68`. Track per-poll
  success / callback result. (Codex.)
- **L5** `range_nm()` only guards negative index; an out-of-range subject value
  would index `kRingLadder` OOB. `adsb_viewmodel.cpp:61-67`. `std::clamp` the
  index. (kimi; defensive.)
- **L6** `cycle_sort()` / `%` on a negative subject value yields a negative sort
  mode. `adsb_viewmodel.cpp:97-99`. Normalize before `%`. (kimi; defensive.)
- **L7** `save_settings()` writes on every keypress — flash wear on real
  hardware (SPIFFS/LittleFS). `adsb_viewmodel.cpp:222`. Debounce / save on idle.
  (kimi.)
- **L8** Signal bar & conn dot use `view::palette(false)` — ignore dark mode.
  `adsb_screen.cpp:234,236,244`. Use `palette(vm_.is_dark_mode())`. (kimi.)
- **L9** Emitter category lookup is case-sensitive (lowercase → "other").
  `aircraft.cpp:35-42`. Upcase before lookup (dump1090 emits uppercase; minor).
  (kimi.)
- **L10** Right side-list marker (`●`/`›`) is prepended but the label is
  right-aligned, so the mark floats to the far right. `adsb_screen.cpp:747-769`.
  Append the mark for the right column. (kimi; cosmetic.)

---

## False positives / non-issues (do not re-investigate)

- **`source.ok()` data race** (kimi #3): FALSE — `ok_` is `std::atomic<bool>`,
  `ok()` returns `ok_.load()`. kimi flagged it conditionally without the toolkit
  header.
- **`upsert` callback use-after-free** (kimi #13): NON-ISSUE — `EntityStore::upsert`
  runs the patch synchronously while holding the mutex; the by-reference capture
  is safe. kimi was uncertain and asked to verify.
- **Partially-applied JSON generation observed by UI** (Codex #4, Gemini note):
  technically true (per-aircraft locking) but benign — aircraft states are
  independent; intra-aircraft updates are already atomic. A batch upsert would
  still be nice for M3/H3, not for correctness.
- **`read_string` may throw on out-of-range integer** (kimi #6): low risk;
  nlohmann stores oversized JSON integers as double (then `is_number_integer()`
  is false). Wrapping in try/catch is cheap hardening but not a live bug.

---

## Suggested order of work

1. **H1** (UB one-liner) + **L1** (NaN clamp) — tiny, pure safety.
2. **M1** (`ADSB_TTL` dead) + **L3** (`has_seen`) — correctness, small.
3. **M2** (settings validation) — robustness, self-contained.
4. **M3** (tick churn): trails-in-place + cell diffing first (cheap, high
   leverage), row caching next.
5. **H2/H3** (freshness semantics) — design decision needed; couple with a batch
   upsert API (also helps M3/M5).
6. Remaining Low as cleanup.
