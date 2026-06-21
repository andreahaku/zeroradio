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
- **`adsb_app`**: dump1090 `aircraft.json` parser (emitter→category, squawk→emergency, `"ground"` alt,
  coord validation) + List / PPI radar / Detail views, driven by a mock `aircraft.json` (no dongle needed).
- **`sdr_app`**: SDR receiver ported from `../SDRTerminal` onto the toolkit — FFT line chart + scrolling
  RGB565 waterfall, S-meter, freq/time grids, passband overlay, manual frequency entry, and audio demod
  (WFM/FM/AM/USB/LSB/CW). Live RTL-SDR via `rtl_tcp` when built with fftw3f + SDL2; otherwise a synthetic
  mock source drives the UI (no dongle needed). State (VFO/mode/…) persists across runs.

## Build & run (desktop SDL simulator)
```bash
cmake --preset linux-x86-64                 # configure (first run fetches LVGL 9.5 + nlohmann/json)
cmake --build --preset linux-x86-64-dbg     # → build/linux-x86-64/apps/{adsb,sdr}/Debug/*_app
./build/linux-x86-64/apps/adsb/Debug/adsb_app   # ADS-B viewer (mock data)
./build/linux-x86-64/apps/sdr/Debug/sdr_app     # SDR receiver (mock source; SDR_SOURCE=mock to force)
# point ADS-B at your own data: ADSB_JSON=/path/to/aircraft.json ./.../adsb_app
# point SDR at a real receiver: SDR_RTLTCP=host:port ./.../sdr_app   (needs rtl_tcp running)
```
Status, architecture, what works vs. what's stubbed, and next steps: see [`HANDOVER.md`](HANDOVER.md).

## License
MIT (matches the SDRTerminal base and the M5Stack template).
