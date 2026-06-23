# Architecture — cardputer-radio

A family of radio apps for the **M5Stack CardputerZero** (a Raspberry-Pi-class Linux ARM64 handheld,
320×170 RGB565 screen, physical keyboard with a 5-key navigation row). One shared **toolkit** plus thin
per-app viewers. Unifying thesis: **decode on a host, view on the device** — each app is essentially a
parser plus a field mapping rendered through the shared toolkit.

- `toolkit/` — the reusable library (app shell, reactive layer, platform, widgets, net, geo, model).
- `apps/adsb/` — ADS-B 1090 MHz aircraft radar/list (data: dump1090 `aircraft.json`, mock bundled).
- `apps/sdr/` — SDR spectrum/waterfall + audio demod (data: `rtl_tcp`, synthetic mock).

Per-app details live in [`apps/adsb/README.md`](../apps/adsb/README.md) and
[`apps/sdr/README.md`](../apps/sdr/README.md). This document covers the shared design.

## Reactive MVVM, one-way data flow

The UI observes LVGL *subjects* and never reads state directly; actions flow one way:

```
input (keys 4-8 + ESC) → key router (toolkit platform/linux_input)
   ├─ 4..8  → NavBar slot → NavProvider::nav_activate(page, slot) → app ViewModel action
   │                                            → model / persisted state + publish subject → UI updates
   ├─ ESC   → quit handler (toolkit run_app)
   └─ capture → while a modal dialog is open (e.g. SDR frequency entry), every key goes to it

live data (independent of input, never blocks the UI):
   source (background reader thread) ─▶ parse/transform ─▶ shared store/state (under a mutex)
   lv_timer → Screen::tick(): snapshot the store, rebuild, draw the active view
```

## The shared toolkit (`radio_toolkit`)

| Area | What it provides |
| --- | --- |
| `app/` | `ShellViewModel` (generic shell subjects: dark mode, toolbar page, quit, title, nav-refresh), `NavProvider` (the interface an app implements to populate the NavBar), `run_app()` (lifecycle: `lv_init` + display + theme + quit handler, then build the screen and run the loop), `AssetManager`. |
| `reactive/` | LVGL-subject bindings (the one-way observe layer). |
| `view/` | Theme, `ui_const.h` (Phosphor icon codepoints), widgets (`IconButton`, `NavBar`, `TitleBar`), `screens/base_screen` (a screen base parameterized on `ShellViewModel&` + `NavProvider&`). |
| `platform/` | Key routing (`linux_input`), and the headless `remote_fb` display+input driver. |
| `net/` | `FileJsonSource` — poll a file on a background thread and hand its contents to a callback. |
| `geo/` | Haversine `range_nm` / `bearing_deg` and a north-up `project()` for the radar. |
| `model/` | `EntityStore` — a thread-safe table of tracked entities (sparse-merge upsert, snapshot, TTL sweep). |

### Shell decoupling (how one NavBar serves different apps)

The widgets are decoupled from any concrete view-model so they can be reused across apps:

- **`ShellViewModel`** owns the generic shell subjects and a pointer to a `NavProvider`.
  `cycle_toolbar()` advances the toolbar page modulo `nav_page_count()` and bumps `nav_refresh`.
- **`NavProvider`** (abstract) is implemented by each app:
  `int nav_page_count()`, `void nav_fill(int page, NavSlot out[5])` (label/font/enabled per slot 1..4;
  slot 0 is the page number, handled by the NavBar), `void nav_activate(int page, int slot)`.
- **`IconButton` / `TitleBar`** take `lv_subject_t*` (dark/title/subtitle), not a concrete view-model.
- **`NavBar`** takes `(parent, ShellViewModel&, NavProvider&, AssetManager&)`. Slot 0 →
  `shell.cycle_toolbar()`; slots 1..4 → `provider.nav_activate()`. It re-renders by observing
  `toolbar_page` + `dark_mode` + `nav_refresh`.
- **`run_app(shell, assets, build_root)`** does `lv_init` + display + theme + quit handler, then
  `build_root()` constructs the screen and starts the loop.

Adding a new app is mostly: a parser, a `NavProvider` mapping keys to actions, and a screen.

## Display backends

`init_display()` in `toolkit/src/app/run_app.cpp` selects the LVGL display driver:

- **SDL** — the desktop simulator (development), native 320×170.
- **DRM / fbdev** — on-device panels.
- **remote-fb** — headless streaming: the app renders headless and streams each flushed framebuffer
  region over TCP while a desktop SDL2 viewer blits it and forwards keystrokes back. Enable on any build
  with `REMOTE_FB=<port>` (or `REMOTE_FB=1` for the default 5800). Full protocol and tooling in
  [`tools/remote-fb/README.md`](../tools/remote-fb/README.md).

## Per-app data paths

**ADS-B** (`apps/adsb`). `FileJsonSource` polls dump1090's `aircraft.json` on a reader thread →
`parse_aircraft_json` → `apply_to_store` sparse-merges each aircraft (keyed by ICAO hex) into the
thread-safe `EntityStore`. An LVGL timer (~300 ms) sweeps stale entries (TTL), snapshots the store,
sorts, records trails, and draws one of four screens (List / Radar / Detail / Settings). The Radar and
Detail mini-radar share one RGB565 rasterizer (`render_scope`). See the app README for the full UI.

**SDR** (`apps/sdr`). `RtlTcpSource` connects to a stock `rtl_tcp` server, reads raw IQ on a reader
thread, and does two things with the same stream: an FFT (FFTW, single precision) feeding the spectrum
chart + waterfall, and a multi-mode demodulator (`AudioDemod`: WFM/NFM/AM/SSB-Weaver/CW) feeding SDL
audio at 48 kHz. Tuning/zoom retune the hardware and re-crop the FFT window. The real source is gated by
`SDR_HAVE_RTLTCP` (set when `fftw3f` + SDL2 are present); otherwise a synthetic mock drives the UI.

## Build

CMake ≥ 3.31, C++17. Presets: `linux-x86-64(-dbg/-rel)` for the desktop SDL simulator,
`cp0-cross(-dbg/-rel)` for the aarch64 device target (needs the BSP sysroot). The top-level CMake fetches
LVGL 9.5 and nlohmann/json on first configure. The real RTL-SDR source is currently desktop-only; the
cross build keeps the SDR mock until `fftw3f` is provisioned in the BSP sysroot.

```bash
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg   # → build/linux-x86-64/apps/{adsb,sdr}/Debug/*_app
```

> **Editor note:** clang in-editor may report false positives (`lvgl.h not found`, `lv_subject_t`/
> `entity_store.h` unknown) because it doesn't see CMake's include paths. The source of truth is
> `cmake --build`.
