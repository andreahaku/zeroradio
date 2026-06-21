# cardputer-radio

A monorepo of **radio-viewer apps** for the **M5Stack CardputerZero** (Linux ARM64, 320×170 RGB565 display),
built on a shared **LVGL toolkit** extracted from [`../SDRTerminal`](../SDRTerminal). Philosophy (see the
design suite in [`../radio-apps`](../radio-apps)): **decode *and* visualize entirely on the device** — an
SDR front-end (RTL-SDR/HackRF) plugs into the Cardputer, a mature decoder runs as a local process, and each
app is a thin **parser + field mapping** over the common viewer toolkit.

```
toolkit/   # reusable LVGL shell + radio widgets (PPI/list/detail), entity store, geo, net sources
apps/
  adsb/    # ADS-B 1090 MHz aircraft viewer (dump1090 aircraft.json)
  sdr/     # SDR receiver: spectrum + waterfall, demod (WFM/FM/AM/USB/LSB/CW), rtl_tcp
```

## What's here
- **`radio_toolkit`** (static lib): generic app shell (`ShellViewModel` + `NavProvider` + `run_app`),
  generalized 5-key NavBar / widgets, `geo` (haversine range/bearing + PPI projection), thread-safe
  `EntityStore` (merge + TTL ageing), `FileJsonSource` (resilient background file poller).
- **`adsb_app`**: dump1090 `aircraft.json` viewer with **four screens** cycled by one key — **List**
  (colour-coded sortable table), **Radar** (north-up scope: aircraft as heading arrows, range rings, side
  callsign lists, position trails), **Detail** (selected aircraft fields + decoded ADS-B status + a
  mini-radar with the same scope as Radar — rings, NM labels and all traffic, selected one highlighted),
  and **Settings** (units, TTL, range, trails, filters, theme — persisted).
  Hex-stable selection, auto-range, an RSSI signal bar, and a configurable home. Runs on a bundled mock
  `aircraft.json` (no dongle) or a live dump1090 feed. See [`apps/adsb/README.md`](apps/adsb/README.md).
- **`sdr_app`**: SDR receiver ported from `../SDRTerminal` onto the toolkit — FFT line chart + scrolling
  RGB565 waterfall, S-meter, freq/time grids, passband overlay, manual frequency entry, and audio demod
  (WFM/FM/AM/USB/LSB/CW). Live RTL-SDR via `rtl_tcp` when built with fftw3f + SDL2; otherwise a synthetic
  mock source drives the UI (no dongle needed). State (VFO/mode/…) persists across runs.
  See [`apps/sdr/README.md`](apps/sdr/README.md).

## Build & run (desktop SDL simulator)
```bash
cmake --preset linux-x86-64                 # configure (first run fetches LVGL 9.5 + nlohmann/json)
cmake --build --preset linux-x86-64-dbg     # → build/linux-x86-64/apps/{adsb,sdr}/Debug/*_app
./build/linux-x86-64/apps/adsb/Debug/adsb_app   # ADS-B viewer (mock data)
./build/linux-x86-64/apps/sdr/Debug/sdr_app     # SDR receiver (mock source; SDR_SOURCE=mock to force)
# live ADS-B: run `dump1090 --write-json <dir>` then ADSB_JSON=<dir>/aircraft.json ./.../adsb_app
# centre the radar on you: ADSB_HOME_LAT=.. ADSB_HOME_LON=.. ./.../adsb_app  (default: Bologna, IT)
# point SDR at a real receiver: SDR_RTLTCP=host:port ./.../sdr_app   (needs rtl_tcp running)
```
Status, architecture, what works vs. what's stubbed, and next steps: see [`HANDOVER.md`](HANDOVER.md).

## License
MIT (matches the SDRTerminal base and the M5Stack template).
