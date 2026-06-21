# Initial multi-model review — cardputer-radio scaffold

> Date: 2026-06-21. Reviewers: **Claude** (Opus 4.8), **Gemini** (3.1 Pro), **Codex** (GPT-5.4),
> each reviewing the scaffold independently; findings below are merged with cross-model agreement noted.
> Scope: toolkit + first ADS-B app, as of the initial commit.

## Verdict
The scaffold is **architecturally sound and builds green**. Core synchronization and LVGL teardown are
correct: `FileJsonSource` joins on shutdown, `EntityStore` snapshots are mutex-guarded, object-bound
observers are removed with their LVGL objects, and the canvas buffer lifetime is valid. The real risks are
**ADS-B optional-field semantics**, **numeric-parse hardening**, and a few **per-device-deployment / perf**
items that matter once this runs on the constrained ARM target with a live feed.

## Already addressed in this session
- **PPI canvas clipped** (Codex `ppi-clipping`, medium): the 150px square exceeded the ~122px body and was
  cut vertically. **Fixed** → `kPpiSize = 120` (4-byte-stride aligned). Heading vectors + pooled callsign
  labels added.
- **NavBar slot-0 wastes a key for single-page apps** (Gemini `navbar-slot0-override` high, Codex
  `navbar-slot-zero` medium): **resolved by design** — slot 0 / key 4 is intentionally always the page
  switcher (maximum flexibility, consistent with SDRTerminal). ADS-B now uses **2 tool pages**, so no key
  is wasted: page 0 = view/sort/range−/range+, page 1 = prev/next/detail/(reserved). `select_prev/next` and
  `open_detail` are now wired (this was the open TODO).
- **HIGH #1 — ADS-B field presence semantics** (Codex `adsb-presence`): **fixed.** `Aircraft` now tracks
  `on_ground` / `has_seen` / `has_category` separately; `apply_to_store` is truly sparse (writes only
  present fields), altitude and ground clear each other, squawk+emergency travel together, and the UI shows
  `grnd` (ground) vs `-` (unknown) vs altitude distinctly. Validated by a 3rd reviewer (kimi via /ollama).
- **HIGH #2 — numeric parse hardening** (Codex `numeric-validation` + Gemini `string-numeric-parsing`):
  **fixed.** `read_number` trims, requires full consumption, rejects `null`/`nan`/`inf`; a `to_ll` helper
  guards every `double→long long` cast against out-of-range UB.

## Open findings (prioritized)

### High — ✅ RESOLVED (see "Already addressed" above)
1. **ADS-B field presence semantics** (Codex `adsb-presence`, `apps/adsb/src/model/aircraft.cpp`).
   - Absent `alt_baro` and the explicit `"ground"` value both yield `has_alt=false` → the UI shows "grnd"
     for *unknown* altitude too. They are different states.
   - `apply_to_store` always writes `category`, `emergency`, `seen` even when not present in the message,
     so the merge is **not truly sparse** (contradicts the documented accumulate-across-message-types
     behavior) and a real landing won't erase a stale numeric altitude.
   - **Fix:** track presence per field; add an explicit `on_ground` state; only merge present fields; clear
     altitude when ground is explicitly reported.
2. **Numeric parsing accepts junk / non-finite** (Codex `numeric-validation` + Gemini `string-numeric-parsing`,
   `aircraft.cpp` `read_number`). `std::stod` accepts `"123junk"`, `"nan"`, `"inf"`; non-finite values then
   reach `double→long long` casts (UB out of range). **Fix:** require full-string consumption + `std::isfinite()`
   + per-field ranges; prefer `std::from_chars` (also exception-free — see perf below).

### Medium
3. **Per-frame deep copy / rebuild churn** (Gemini `store-snapshot-copy` + Codex `refresh-churn`). Every
   300 ms the UI deep-copies the whole `EntityStore` (nested `std::map<string,string>` per aircraft), sorts,
   and rewrites the table — though the source only polls every 2000 ms. **Fix:** a store generation counter /
   dirty flag to rebuild rows only on change, and/or a locked `for_each` visitor instead of `snapshot()`.
4. **`std::stol` exceptions in the hot path** (Gemini `string-numeric-parsing`, `adsb_screen.cpp:build_rows`).
   try/catch `std::stol` per numeric field per aircraft at 3 Hz is expensive on embedded. **Fix:** `std::from_chars`.
5. **Reader-callback exception → `std::terminate`** (Codex `source-callback`, `file_json_source.cpp`). An
   exception from `on_json_` escapes the `std::thread` boundary and kills the process. **Fix:** wrap the
   callback in try/catch, log, decide whether to keep polling.
6. **Provider ownership duplicated** (Codex `provider-duplication`, `shell_viewmodel.h`). `ShellViewModel`
   holds a nullable raw `NavProvider*` for page counts while `BaseScreen`/`NavBar` get a separate reference;
   a 2nd app could forget `set_nav_provider()` or pass a different one. **Fix:** single provider dependency
   (e.g. NavBar passes `provider_.nav_page_count()` when cycling).
7. **Device deployment paths** (Codex `device-assets`, `CMakeLists.txt`). Asset/mock paths embed host source
   dirs; no install rules package fonts or data → a cross-built binary loses its icon font / default data.
   **Fix:** GNUInstallDirs install rules + a stable runtime data dir + an explicit device default for the live
   `aircraft.json`.
8. **Polling sleep wakes CPU 20×/s** (Gemini `filejson-polling-sleep`). The 50 ms step loop prevents deep
   sleep. **Fix:** `std::condition_variable::wait_for(poll_ms)` woken by stop.

### Low
9. **Source health latches true** (Codex `source-health`). `ok_` is set on first success and never reset, so
   the conn dot stays green even if the file later disappears. **Fix:** per-poll success flag / last-success
   timeout.
10. **Display leak on `build_root()` failure** (Gemini `display-leak`, `run_app.cpp`). `lv_display_delete`
    not called before `return 1`. (Cosmetic; OS reclaims on exit.)
11. **`Threads::Threads` not linked to `radio_toolkit`** (Codex `threads-linkage`). Builds here, but may omit
    flags on other toolchains. **Fix:** explicit link.
12. **Canvas stride guard** (Gemini `canvas-stride-overflow`, high — but Codex confirmed the current size is
    correctly 4-byte aligned). Not triggered at 120px, but make the buffer size derive from
    `lv_draw_buf_width_to_stride` if `kPpiSize` ever changes to a non-aligned value.

## Cross-model agreement summary
- **Both Gemini + Codex** independently flagged: NavBar slot-0 key cost (resolved by design), per-frame
  store copy/rebuild churn, and exception-based numeric parsing. High confidence on those three.
- **Codex** went deeper on correctness/semantics (field presence, numeric validation, source robustness,
  deployment). **Gemini** went deeper on perf/LVGL-buffer mechanics. **Claude** confirmed the thread/lifetime
  model is sound and that selection-by-row-index (vs. by hex id) will drift under live re-sorting — worth
  tracking selection by `hex` when the feed is live.
