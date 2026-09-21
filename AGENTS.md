# AGENTS.md — ZeroRadio (cardputer-radio)

Context for any coding session in this repository. Read this first, then the README of the app you touch and `docs/architecture.md`.

## What this is

ZeroRadio is a suite of radio apps for the [M5Stack CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero): a Linux ARM64 handheld (Raspberry Pi CM0, Debian 13 trixie) with a 320x170 RGB565 screen and a keyboard. An RTL-SDR dongle in the USB-A port feeds every app. Each app starts its own decoder on the device and stops it on exit.

- `toolkit/`: the shared library. App shell and run loop, reactive subjects (MVVM), key routing, NavBar, themes, vector map, geo, location, sources, text viewer. See `toolkit/README.md`.
- `apps/radio`: the hub (the single launcher entry "ZeroRadio"). It lists the installed apps, launches one at a time and shows About.
- `apps/sdr`: spectrum, waterfall and audio. Decoder: `rtl_tcp` (Debian `rtl-sdr`).
- `apps/survey`: shown as "Scanner". Wide-band sweep and peaks. Decoder: `rtl_power` / `hackrf_sweep`.
- `apps/ism`: 433/868 MHz device sniffer. Decoder: `rtl_433`.
- `apps/adsb`: aircraft list, radar and map. Decoder: bundled `readsb`.
- `apps/ais`: ship list, radar and map. Decoder: bundled `AIS-catcher`.
- `apps/meshtastic`: Meshtastic client over `meshtasticd`. Not in the 1.0.0 package.
- `docs/help/<app>.md`: the in-app help pages (key H). `CHANGELOG.md` and `CREDITS.md` feed the hub About page.

## Build and run

```bash
# Desktop simulator (SDL window, 320x170)
cmake --preset linux-x86-64 && cmake --build --preset linux-x86-64-dbg
./build/linux-x86-64/apps/adsb/Debug/adsb_app          # ADSB_SOURCE=mock for sample data

# Device build and package, in a Debian trixie container (needs Docker)
scripts/cp0-docker-build.sh                             # binaries: build/cp0-trixie
scripts/cp0-docker-build.sh --package                   # zeroradio_<version>_arm64.deb
```

Build for the device with the container, not with a host cross toolchain: the binaries must match the device's glibc and libstdc++ (trixie). `cmake/decoders.cmake` builds the bundled readsb and AIS-catcher from pinned tags.

## Tests: run them before a change is done

```bash
ctest --test-dir build/linux-x86-64 -C Debug --output-on-failure
```

Nine tests: waterfall_scroll, map_render (culling must not change a pixel), location (city search, coordinates, NMEA, USB GPS on a pseudo-terminal, save/load), and the decoder parity tests aircraft_parse, meshtastic_decoder, ais_decoder, nmea_net, sweep_parser, ism_parse. The parity tests are frozen: never edit their vectors to make code pass.

## How the pieces fit

- Keys (`toolkit/src/platform/linux_input.cpp`): digits 4-8 drive the NavBar. Esc goes back, holding Esc 3 s returns to the system launcher (exit code `ShellViewModel::kExitHome`, the hub then closes too). F/X (up/down) call `on_up()`/`on_down()`, TAB calls `on_tab()`, H opens the help set with `set_help_doc()`. A dialog takes the keyboard with `platform::set_key_capture()`; pass `text=true` only for free-text entry, because menus rely on F/X/Z/C as arrows.
- Decoders: `toolkit::ChildService` starts a helper tool, drains its stdout, respawns it and stops it with the app. `toolkit::find_tool()` prefers a copy bundled next to the executable. Children get `PR_SET_PDEATHSIG`, so a killed app never leaves a tool holding the dongle.
- App switches: `ShellViewModel::request_handoff()` plus `toolkit::run_handoff()` exec a sibling app in place (same PID), after `run_app` has released the display. The hub waits on that PID, so only one process draws at a time. Scanner "Open in SDR" and TAB use it.
- Location: `toolkit::location` holds the shared position (`~/.config/zeroradio/location`), the offline city list (`assets/geodata/cities.tsv`, GeoNames) and `GnssReader` (USB GPS or the Cap LoRa-1262-GPS shield). `toolkit::LocationDialog` is the UI.
- Map: `toolkit/src/map` draws the Natural Earth base map. `draw_base()` culls geometry outside the view and `BaseMapCache` replays an unchanged view. The Light theme uses `MapStyle::day()`.
- Settings live in `~/.config/zeroradio/<app>/`.

## Remote framebuffer (optional)

`REMOTE_FB=<port>` runs any app headless and streams its screen to the desktop viewer in `tools/remote-fb/` (keys are forwarded back). See `tools/remote-fb/README.md`. A new headless path must install its own LVGL tick and delay callbacks, as `remote_fb_create()` does.

## Conventions

- English everywhere: code, comments, docs, commits.
- Commit messages: Conventional Commits.
- Keep the desktop build and the tests green; build the package in the container before a release.
- Follow the existing patterns (MVVM with reactive subjects, a source feeding an `EntityStore`). A new app should be mostly a parser plus a field mapping.
- Files started from the [M5Stack template](https://github.com/CardputerZero/Template) keep the M5Stack copyright line; new files carry One Small Step Apps Ltd. Every C/C++ file has an SPDX header.
