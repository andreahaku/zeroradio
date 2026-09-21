# Architecture — ZeroRadio

ZeroRadio is a family of radio apps for the **[M5Stack CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero)**: a Linux ARM64 handheld
(Raspberry Pi CM0, Debian 13 trixie) with a 320×170 RGB565 screen and a keyboard ([hardware documentation](https://docs.m5stack.com/en/CardputerZero)). One shared
**toolkit** carries the shell, widgets, maps and data sources. Each app adds a parser, a field
mapping and its screens.

Every decoder runs **on the device**. The RTL-SDR dongle plugs into the CardputerZero USB-A port
(hardware V0.6). Each app starts its own decoder when it opens and stops it when it exits, so one
app at a time owns the dongle.

- `toolkit/`: the shared library (app shell, reactive layer, platform, widgets, raster, map, net,
  geo and location, model, logger, config). See [`toolkit/README.md`](../toolkit/README.md).
- `apps/radio/`: the **hub**, the single launcher entry "ZeroRadio". It lists the installed apps,
  runs one at a time and shows About.
- `apps/sdr/`: spectrum, waterfall and audio demodulation (decoder: `rtl_tcp`).
- `apps/survey/`: shown as **Scanner**, a wide-band sweep and peaks list (decoder: `rtl_power` or
  `hackrf_sweep`).
- `apps/adsb/`: ADS-B 1090 MHz aircraft list, radar and map (decoder: bundled `readsb`).
- `apps/ais/`: AIS vessel list, radar and map (decoder: bundled `AIS-catcher`).
- `apps/ism/`: ISM 433/868 MHz device sniffer (decoder: `rtl_433`).
- `apps/meshtastic/`: Meshtastic client over a local `meshtasticd`. Not in the 1.0.0 package.

Each app README (`apps/<app>/README.md`) covers its UI. This document covers the shared design.

## Reactive MVVM, one-way data flow

The UI observes LVGL *subjects* and never reads state directly. Actions flow one way:

```
input (keyboard) → key router (toolkit platform/linux_input)
   ├─ 4..8     → NavBar slot → NavProvider::nav_activate(page, slot) → app ViewModel action
   │                                            → model / persisted state + publish subject → UI updates
   ├─ F/X, ↑/↓ → ShellViewModel::on_up() / on_down()   (move the cursor of the visible list)
   ├─ TAB      → ShellViewModel::on_tab()              (SDR ↔ Scanner)
   ├─ H        → help page (ShellViewModel::help_doc(), shown in a TextViewer)
   ├─ ESC      → short press: back / quit (run_app); hold 3 s: exit with kExitHome
   └─ capture  → while a modal dialog is open, every key goes to it

live data (independent of input, never blocks the UI):
   decoder process ─▶ source (background reader thread) ─▶ parse ─▶ shared store (under a mutex)
   lv_timer → Screen::tick(): snapshot the store, rebuild, draw the active view
```

## The shared toolkit (`radio_toolkit`)

| Area | What it provides |
| --- | --- |
| `app/` | `run_app()` (lifecycle: display, theme, key handlers, help viewer, loop; returns the shell's exit code), `ShellViewModel` (shell subjects, `on_tab`/`on_up`/`on_down`, `set_help_doc`, `request_home`, `request_handoff`), `NavProvider`, `handoff` (`run_handoff`, `find_sibling`), `docs` (`read_doc`), `AssetManager`, `persisted_state`. |
| `reactive/` | LVGL-subject bindings (the one-way observe layer). |
| `view/` | Theme, `ui_const.h` (Phosphor icon codepoints), `raster` (the RGB565 primitives and the waterfall colormap), `screens/base_screen`, and the widgets: `NavBar`, `IconButton`, `TitleBar`, `BatteryBadge`, `TextViewer` (+ `render_markdown`), `LocationDialog`. |
| `map/` | The vector base map: `VectorMap` (loads `.rmap` files) and `map_renderer` (`draw_base` with culling, `BaseMapCache`, `MapStyle` and `MapStyle::day()`). Used by the ADS-B, AIS and Meshtastic map screens. See [`mapdata-design.md`](mapdata-design.md). |
| `platform/` | Key routing (`linux_input`), `Subprocess` (fork/exec with a non-blocking stdout pipe), `ChildService` and `find_tool()` (the decoder lifecycle), `battery` (the BQ27220 gauge), and the headless `remote_fb` display and input driver. |
| `net/` | `FileJsonSource` (poll a file on a background thread) and `NmeaNetSource` (NMEA lines over UDP/TCP with multi-fragment reassembly). |
| `geo/` | Haversine `range_nm` / `bearing_deg`, the azimuthal `project()` for the radar, `project_mercator()` and `clip_segment()` for the map, and `location` (shared position, offline city search, `GnssReader`). |
| `model/` | `EntityStore`: a thread-safe table of tracked entities (sparse-merge upsert, snapshot, TTL sweep). |
| `logger/` | A small file/stderr logger. |
| `config/` | `app_config.h` (config-file path helper) and the LVGL configs (desktop / device). |

### Shell decoupling (how one NavBar serves different apps)

The widgets depend on no concrete view-model, so every app reuses them:

- **`ShellViewModel`** owns the generic shell subjects and a pointer to a `NavProvider`.
  `cycle_toolbar()` advances the toolbar page modulo `nav_page_count()` and bumps `nav_refresh`.
  An app's view-model derives from it and overrides `on_tab()`, `on_up()` and `on_down()` where
  they mean something.
- **`NavProvider`** (abstract) is implemented by each app:
  `int nav_page_count()`, `void nav_fill(int page, NavSlot out[5])` (label/font/enabled per slot 1..4;
  slot 0 is the page number, handled by the NavBar), `void nav_activate(int page, int slot)`.
- **`IconButton` / `TitleBar`** take `lv_subject_t*` (dark/title/subtitle), not a concrete view-model.
- **`NavBar`** takes `(parent, ShellViewModel&, NavProvider&, AssetManager&)`. Slot 0 →
  `shell.cycle_toolbar()`; slots 1..4 → `provider.nav_activate()`. It re-renders by observing
  `toolbar_page` + `dark_mode` + `nav_refresh`.
- **`run_app(shell, assets, build_root)`** initialises LVGL, the display, the theme and the key
  handlers, then calls `build_root()` to construct the screen and runs the loop.

A new app needs a parser, a `NavProvider` that maps keys to actions, and a screen.

### Keys and the CardputerZero conventions

`toolkit/src/platform/linux_input.cpp` reads the keyboard and routes each key:

- Digits 4-8 drive the NavBar slots.
- F/X (and the arrow keys) call `on_up()` / `on_down()`. Z/C act as left/right inside dialogs.
- TAB calls `on_tab()`. H opens the page set with `set_help_doc()`.
- A short ESC press goes back or quits. Holding ESC shows a hint after 0.5 s and, at 3 s, calls
  `request_home()`: the app exits with `ShellViewModel::kExitHome` and the hub closes too, which
  returns the user to the system launcher.
- A dialog takes the keyboard with `platform::set_key_capture()`. Pass `text=true` only for
  free-text entry: in text mode F/X/Z/C type letters, and menus rely on them as arrows.

## Decoders on the device

Each app owns its decoder process. Three toolkit pieces handle the lifecycle:

- **`toolkit::ChildService`** starts a helper tool on a background thread, drains its stdout so a
  full pipe never blocks it, restarts it after a fixed pause (2 s by default) when it exits, and
  stops it in its destructor, when the app quits. The app talks to the tool through the tool's own
  channel (a socket, a JSON file or UDP).
- **`toolkit::find_tool()`** returns the copy bundled next to the executable when one exists, else
  the bare name for a `PATH` lookup.
- **`toolkit::Subprocess`** sets `PR_SET_PDEATHSIG` on every child. A killed app never leaves a tool
  holding the dongle.

| App | Decoder | How the app starts it | Data channel |
| --- | --- | --- | --- |
| SDR | `rtl_tcp` (Debian `rtl-sdr`) | `RtlTcpSource` owns a `ChildService` (`spawn_local_server`) | TCP `127.0.0.1:1234` |
| ADS-B | `readsb` (bundled) | `ChildService` in `main.cpp` | `aircraft.json` in `$XDG_RUNTIME_DIR/zeroradio/adsb`, read by `FileJsonSource` |
| AIS | `AIS-catcher` (bundled), with `-X off` | `ChildService` in `main.cpp` | UDP `127.0.0.1:10110`, read by `NmeaNetSource` |
| ISM | `rtl_433 -F json` (Debian `rtl-433`) | `IsmSource` runs a `Subprocess` and restarts it itself | JSON lines on stdout |
| Scanner | `rtl_power` / `hackrf_sweep` | `CsvSweepSource` runs a `Subprocess` and restarts it itself | CSV on stdout |

ISM and Scanner read the tool's stdout, so they drive `Subprocess` directly instead of
`ChildService`. `-X off` stops AIS-catcher from sharing received data with the aiscatcher.org feed.

Environment variables select another source for development: `SDR_RTLTCP=<host>:<port>` connects
to an existing `rtl_tcp` server instead of starting one, `ADSB_JSON=<file>` reads an existing
`aircraft.json`, `AIS_UDP`/`AIS_TCP`/`AIS_NMEA` read external NMEA, and `*_SOURCE=mock` uses the
bundled sample data. Each app README lists its variables.

## Launching apps: the hub and hand-off

The hub (`apps/radio`) resolves each app binary next to its own executable (or in the dev build tree) and hides apps whose
binary is absent. To run one, it releases the display, starts the app with `posix_spawn()` and waits
for it. When the app exits, the hub re-executes itself to show the menu with a fresh LVGL state. An
exit code of `kExitHome` closes the hub instead.

An app can switch to a sibling without going through the menu.
`ShellViewModel::request_handoff(bin, app_dir, env)` quits the app, and `main()` then calls
`toolkit::run_handoff()`. That call runs after `run_app()` has released the display and devices, and
it `exec`s the sibling in place. The PID stays the same, so the hub keeps waiting on it and only one
process draws at a time. Scanner uses it for "Open in SDR" (with `SDR_FREQ=<peak>`), and SDR and
Scanner use it for TAB.

## Location

`toolkit::location` holds the position the whole suite shares (the ADS-B and AIS radar centre):

- `load()` / `save()` read and write `~/.config/zeroradio/location`.
- `CityIndex` searches the offline city list `assets/geodata/cities.tsv` (GeoNames cities15000,
  34,146 cities, built by `tools/geodata/build_cities.py`).
- `parse_coords()` accepts typed coordinates such as `35.9, 14.51`.
- `GnssReader` looks for a receiver on a background thread: `$ZERORADIO_GPS_DEVICE` if set, then a
  USB GPS on `/dev/ttyACM*` or `/dev/ttyUSB*`, then the Cap LoRa-1262-GPS shield on the HAT port.
  For the shield it switches on the HAT 5 V rail and restores it afterwards.

`toolkit::LocationDialog` is the UI: type a city or coordinates, or pick the GPS row.

## Display backends

`init_display()` in `toolkit/src/app/run_app.cpp` selects the LVGL display driver:

- **SDL**: the desktop simulator (development), native 320×170.
- **fbdev** (`/dev/fb0`, or `LV_LINUX_FBDEV_DEVICE`): the device build. A DRM driver is available
  with `APP_USE_DRM`.
- **remote-fb**: headless streaming. The app renders without a screen and streams each flushed
  region over TCP to a desktop viewer, which forwards keys back. Enable it on any build with
  `REMOTE_FB=<port>` (or `REMOTE_FB=1` for port 5800). See
  [`tools/remote-fb/README.md`](../tools/remote-fb/README.md).

## Per-app data paths

**ADS-B** (`apps/adsb`). `readsb` writes `aircraft.json` every second. `FileJsonSource` polls it on a
reader thread → `parse_aircraft_json` → `apply_to_store` sparse-merges each aircraft (keyed by ICAO
hex) into the thread-safe `EntityStore`. An LVGL timer (~300 ms) sweeps stale entries (TTL),
snapshots the store, sorts, records trails and draws one of four screens (List / Radar / Detail /
Settings). The scopes draw with `view::raster` and the base map with `BaseMapCache`.

**AIS** (`apps/ais`). `NmeaNetSource` receives `!AIVDM` lines from AIS-catcher over UDP → the pure
`ais_decoder` library (`parse_aivdm` + `AivdmReassembler`, types 1/2/3 and 5) → `apply_to_store`
keyed by MMSI, into the same `EntityStore` and screen machinery as ADS-B.

**SDR** (`apps/sdr`). `RtlTcpSource` starts `rtl_tcp`, connects to it and reads raw IQ on a reader
thread. The same stream feeds an FFT (FFTW, single precision) for the spectrum and waterfall, and a
demodulator (`AudioDemod`: WFM/NFM/AM/SSB-Weaver/CW) for audio at 48 kHz: SDL on the desktop, ALSA
on the device through the `pipewire` PCM. Tuning and zoom retune the hardware and re-crop the FFT
window. The sample rate follows the zoom span, capped by `SDR_SAMPLE_RATE` (the launcher sets
1.024 Msps). Builds without FFTW (`SDR_HAVE_RTLTCP` unset) use a synthetic mock.

**Scanner** (`apps/survey`). `CsvSweepSource` runs `rtl_power` or `hackrf_sweep` on a worker thread
and restarts it after a pause if it exits. The pure `SweepAccumulator` stitches the CSV rows into a
wide normalised frame and a peaks list, which feed the waterfall and PEAKS screens.

**ISM** (`apps/ism`). `IsmSource` runs `rtl_433 -F json` through `toolkit::Subprocess` on a worker
thread. It buffers partial lines, restarts the tool after a pause, and also offers a file replay and
a mock mode. The pure `parse_ism_json_line` decodes each line into an `IsmReading`, and
`apply_to_store` keys it by `device_key()` into the same `EntityStore`. The screens are List, Detail
and Settings. ISM devices have no position, so there is no radar.

**Meshtastic** (`apps/meshtastic`). `MeshtasticClientSource` (TCP `127.0.0.1:4403`, `0x94C3`-framed
protobuf) pumps raw `FromRadio` bytes into the pure `MeshDecoder` (nanopb). The decoder dispatches
nodes, messages and channels into `EntityStore` / `MessageLog` / `ChannelTable`, and five screens
render the mesh. On the device a native `meshtasticd` drives the M5Stack Cap LoRa-1262 (SX1262 +
GNSS). See [`cap-lora-1262.md`](cap-lora-1262.md).

## Help, About and battery

- `toolkit::read_doc()` reads text files by their repository-relative path: from the source tree on
  the desktop, from `/usr/share/zeroradio` on the device. The help pages live in `docs/help/<app>.md`.
  The hub About page reads `CHANGELOG.md` and `CREDITS.md`.
- `view::widgets::TextViewer` shows these pages full screen. `render_markdown()` renders the small
  Markdown subset they use.
- `view::widgets::BatteryBadge` shows the charge level in an app header. It reads
  `platform::read_battery()` (the BQ27220 gauge under `/sys/class/power_supply`) every 10 s and hides
  itself where there is no battery.

## Tests

Nine CTest targets run on the desktop preset. The decoder tests are **frozen parity tests**: two
independent sources must produce the same vectors, and nobody edits the vectors to make code pass.

- `aircraft_parse_test`: ADS-B JSON.
- `ais_decoder_test`: AIVDM against gpsd and pyais.
- `nmea_net_test`: UDP loopback integration.
- `meshtastic_decoder_test`: FromRadio parity.
- `sweep_parser_test`: rtl_power CSV against a Python oracle, with a captured FM sweep.
- `ism_parse_test`: rtl_433 JSON against a Python oracle, with real off-air TPMS captures.
- `waterfall_scroll_test`: the shared waterfall scroll.
- `map_render_test`: culled and unculled `draw_base` output must match pixel for pixel.
- `location_test`: city search, coordinates, NMEA parsing, a USB GPS on a pseudo-terminal, and
  save/load.

```bash
ctest --test-dir build/linux-x86-64 -C Debug --output-on-failure
```

## Build and package

CMake ≥ 3.31, C++17. The top-level CMake fetches LVGL 9.5 and nlohmann/json on first configure.

Desktop simulator (SDL window, 320×170):

```bash
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg   # → build/linux-x86-64/apps/<app>/Debug/<app>_app
```

Device build, inside a Debian trixie container:

```bash
scripts/cp0-docker-build.sh               # binaries in build/cp0-trixie
scripts/cp0-docker-build.sh --package     # also zeroradio_<version>_arm64.deb (CPack)
```

The script builds `docker/cp0-build/Dockerfile` and runs the `cp0-trixie` preset inside it. The
container cross-compiles against trixie's own arm64 libraries, so the binaries need exactly the
glibc and libstdc++ the device has. A host cross toolchain with a newer glibc/libstdc++ produces
binaries the device cannot load. `cmake/decoders.cmake` builds the bundled `readsb` and
`AIS-catcher` from pinned tags.

The `zeroradio` package installs:

| Path | Contents |
| --- | --- |
| `/usr/share/zeroradio/bin/` | `radio_app` (the hub), the app binaries, `readsb`, `AIS-catcher`, `radio-launch.sh` |
| `/usr/share/zeroradio/fonts/`, `mapdata/`, `geodata/` | Runtime assets for the `AssetManager` |
| `/usr/share/zeroradio/mock/` | Sample data for the `*_SOURCE=mock` modes |
| `/usr/share/zeroradio/docs/help/`, `CHANGELOG.md`, `CREDITS.md` | Text for `read_doc()` |
| `/usr/share/APPLaunch/applications/zeroradio.desktop` | The single launcher entry (runs `radio-launch.sh`) |

The package depends on `rtl-sdr`, `rtl-433`, `hackrf` and `pipewire-alsa`. `radio-launch.sh` sets
`SDR_SAMPLE_RATE` and `SDR_ALSA_DEV=pipewire`, then execs the hub. Each app keeps its settings in
`~/.config/zeroradio/<app>/`.

> **Editor note:** clang in the editor may report false positives (`lvgl.h not found`,
> `lv_subject_t` unknown) because it does not see CMake's include paths. `cmake --build` is the
> source of truth.
