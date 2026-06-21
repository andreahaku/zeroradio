# ADS-B — a live aircraft radar for the M5Stack CardputerZero (`apps/adsb`)

The **ADS-B** app turns the [M5Stack CardputerZero](https://docs.m5stack.com/) (a Raspberry-Pi-class
handheld running Linux ARM64, 320×170 RGB565 display, physical keyboard) into a pocket **live aircraft
viewer**: a sortable traffic **list**, a north-up **radar** scope, a per-aircraft **detail** view, and
persistent **settings** — fed by a [dump1090](https://github.com/flightaware/dump1090) decoder reading
an RTL-SDR dongle on 1090 MHz.

It is a graphical [LVGL](https://lvgl.io/) application (not a TUI) built on the shared
**`radio_toolkit`** (the reactive MVVM shell/widgets shared with the `apps/sdr` receiver). It is a pure
**viewer**: decoding is done by `dump1090`, and the app polls the `aircraft.json` file it writes.
Development happens on a desktop **SDL simulator** at the exact device resolution (320×170).

> Status (2026-06-21): building green on the desktop SDL simulator. Verified end-to-end on hardware
> with a real **RTL-SDR Blog V4** feeding `dump1090 --write-json`: live traffic on the list and radar.

## Features

- **Four screens, one key** — **List** (colour-coded, sortable traffic table), **Radar** (north-up PPI
  scope), **Detail** (selected-aircraft fields + decoded status + a mini-radar), and **Settings**
  (persisted). Key `4` cycles the screens; the other four keys act on the current one.
- **North-up radar scope** — three concentric range rings with NM scale labels, a north tick and home
  dot at centre, every positioned aircraft drawn as a **heading arrowhead** (colour-coded by category,
  selected one orange and larger, emergency red), optional position **trails**, and side callsign
  lists. Rendered straight into an RGB565 canvas (`render_scope()`), shared by the Radar and Detail
  mini-radar so they look identical.
- **Auto / manual range** — a range ladder (10/20/50/100/200 NM) plus an **AUTO** state that fits the
  outer ring to the farthest aircraft. Zoom in/out from the Radar or Detail page.
- **Category colours** — light (green), small (cyan), large (blue), heavy (orange), rotorcraft
  (purple), other (grey); any **emergency squawk** (7500/7600/7700) overrides to red.
- **Hex-stable selection** — a moving **cursor** (list highlight) distinct from a locked **selection**
  (the Detail/Radar focus, marked ● in the list), both tracked by ICAO hex so they survive re-sorts.
- **Decoded status** — the Detail view spells out HIJACK 7500 / RADIO FAIL 7600 / EMERGENCY 7700 /
  ON GROUND / nominal, alongside range, bearing, altitude, speed, track, squawk, category and RSSI.
- **Signal + connection feedback** — header RSSI bar from the strongest contact and a connection dot
  tracking the live feed health.
- **Full preference persistence** — theme, units (NM/km), TTL, range, trail length, ground/emergency
  filters, **trails on/off, sort mode and Detail show-others** are all restored on the next launch
  (`$XDG_CONFIG_HOME/cardputer_radio/adsb/settings`), saved atomically on every change.
- **Mock source fallback** — with no `ADSB_JSON` set, the app reads a bundled `aircraft.json`, so the
  whole UI works with no dongle and no decoder.

## How it works

**The data path, end to end.** An RTL-SDR dongle is plugged into USB and runs `dump1090`, which decodes
1090 MHz ADS-B and writes a full `aircraft.json` snapshot to disk every second. The app's
`FileJsonSource` polls that file on a background thread (default every 2 s), parses it, and merges each
aircraft into a thread-safe `EntityStore`. The UI never blocks on I/O.

1. **Parse + merge** — `parse_aircraft_json` turns each record into an `Aircraft` (tolerant of missing
   fields; never throws), then `apply_to_store` merges it into the store keyed by ICAO **hex**.
   Position freshness uses dump1090's `seen_pos`: a position older than the TTL is dropped so the radar
   stops plotting a stale spot.
2. **Snapshot + render** — an LVGL timer (~300 ms) takes a store snapshot, expires stale aircraft (TTL
   sweep), builds a sorted row set, records trails, and refreshes the active screen.

**What you see.** The header shows screen-relevant info on the left (traffic count / range / …), the
selected aircraft's callsign + active-sort value in the centre, and an RSSI bar + connection dot on the
right. The body is one of the four screens.

**How you drive it.** There is no touch or mouse — everything is the five physical keys `4`–`8`. Key
`4` cycles the four screens (and shows the page number); the other four keys act on the current screen.

**It remembers.** Every preference — including the Radar/Detail view toggles — is saved the moment you
change it and restored next launch.

## Quick start (desktop, with a real RTL-SDR + dump1090)

```shell
# 1. Dependencies (Arch shown). The RTL-SDR Blog V4 needs the rtl-sdr-blog driver.
#    nlohmann-json is optional (CMake fetches v3.11.3 if it's missing).
sudo pacman -S --needed cmake ninja sdl2 fmt libpng libjpeg-turbo freetype2 zlib nlohmann-json \
                        rtl-sdr dump1090

# 2. Build the monorepo simulator (from the repo root)
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg

# 3. Start dump1090 writing JSON (plug the dongle first); leave it running
dump1090 --device-index 0 --write-json /tmp/adsb-json --write-json-every 1 --quiet

# 4. Run, pointed at the feed
ADSB_JSON=/tmp/adsb-json/aircraft.json ./build/linux-x86-64/apps/adsb/Debug/adsb_app
```

No dongle? Just run `./build/linux-x86-64/apps/adsb/Debug/adsb_app` — with no `ADSB_JSON` it reads the
bundled mock `aircraft.json` and the whole UI is usable.

Environment overrides:

| Variable | Effect |
| --- | --- |
| `ADSB_JSON=/path/aircraft.json` | Read this dump1090 feed instead of the bundled mock. |
| `ADSB_HOME_LAT`, `ADSB_HOME_LON` | Radar home position (default Bologna, IT: `44.49, 11.34`). |
| `ADSB_TTL=<seconds>` | Drop an aircraft this long after its last update (default `30`). |

## Controls

The bottom NavBar maps the five physical keys `4`–`8` to five slots. The **leftmost key (`4`) cycles
the screen** (List → Radar → Detail → Settings) and shows the page number.

| Key | List | Radar | Detail | Settings |
| --- | --- | --- | --- | --- |
| `4` | screen switch | screen switch | screen switch | screen switch |
| `5` | **cycle sort** (CALL/DST/SPD/ALT/TRK) | zoom in | zoom in | setting ▲ |
| `6` | previous aircraft (▲) | zoom out | zoom out | setting ▼ |
| `7` | next aircraft (▼) | trails on/off | trails on/off | change value |
| `8` | select / deselect (✓) | — | **show others** on/off | quit |

The **Settings** screen is a navigable name|value list: Theme, Units (NM/km), TTL (15/30/60/120 s),
Range (ladder + AUTO), Trails (All/15/30/60 points), Ground (show/hide), Emergency-only (on/off).

## Architecture

Reactive MVVM with a one-way data flow; the UI observes LVGL *subjects* and never reads state directly.
The shell (key routing, NavBar, BaseScreen, reactive bindings, theme, assets, run loop) plus the
`EntityStore`, `FileJsonSource` and `geo` helpers are the shared `radio_toolkit`; only the ADS-B
pieces live in this app.

```
input (keys 4-8) → key router (toolkit) → NavBar slot → NavProvider::nav_activate(page,slot)
                                                          → AdsbViewModel action → subject / persisted settings

live data (independent of input):
   RTL-SDR ──USB──▶ dump1090 ──aircraft.json──▶ FileJsonSource (reader thread, ~2 s poll)
                                                   └─ parse_aircraft_json ─▶ apply_to_store ─▶ EntityStore (mutex)
   lv_timer (~300 ms) → AdsbScreen::tick(): sweep TTL, snapshot, sort, record trails, draw active screen
```

ADS-B-specific files (`apps/adsb/src/`):

- **`model/aircraft.{h,cpp}`** — the dump1090 `aircraft.json` parser (`Aircraft` struct + tolerant,
  non-throwing JSON parse) and `apply_to_store`, the sparse merge into the generic `EntityStore`.
- **`viewmodel/adsb_viewmodel.{h,cpp}`** — app state (current screen, sort, range ladder/AUTO,
  cursor vs locked selection, view toggles), the Settings model with **v2 persistence**, and the
  `NavProvider` mapping keys to actions, on the toolkit's `ShellViewModel`.
- **`view/adsb_screen.{h,cpp}`** — the four screens, the shared `render_scope()` RGB565 rasterizer
  (rings, NM labels, heading arrows, trails), the list/detail/settings tables, header and indicators,
  on the toolkit's `BaseScreen`.
- **`main.cpp`** — wiring: resolve the JSON source + home/TTL env overrides, start the `FileJsonSource`
  poller, and run the toolkit app loop.

Everything else (reactive bindings, NavBar/IconButton, key routing, theme, asset manager, the
`EntityStore`/`FileJsonSource`/`geo` toolkit primitives, run loop) is reused from `radio_toolkit`.

## Build

CMake ≥ 3.31, C++17. The only extra dependency is **nlohmann/json** (header-only): CMake uses the
system package if present, otherwise fetches `v3.11.3` automatically. Built as part of the monorepo:

```shell
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg   # → build/linux-x86-64/apps/adsb/Debug/adsb_app
# release: cmake --build --preset linux-x86-64-rel
```

> **Editor note:** clang in-editor may report false positives (`lvgl.h not found`, `lv_subject_t
> unknown`, `entity_store.h not found`) because it doesn't see CMake's include paths. The source of
> truth is `cmake --build`.

The cross build (`cp0-cross`) and `.deb` packaging are monorepo-wide concerns (pending hardware).

## Roadmap

- `HttpJsonSource` in the toolkit: poll `dump1090`'s HTTP endpoint (`/data/aircraft.json`) directly,
  so no shared file is needed.
- `DecoderSupervisor`: spawn/stop/retune a local `dump1090` from the app, enabling decoder settings
  (gain / ppm / bias-tee) on the Settings screen.
- Add screenshots / a demo recording to `docs/media/`.
- On-device `.deb` deployment.

## License

MIT. See the repo `assets/` folder for third-party asset license notes.
