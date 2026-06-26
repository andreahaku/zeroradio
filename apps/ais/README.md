# AIS — marine traffic viewer for the M5Stack CardputerZero (`apps/ais`)

The **AIS** app shows nearby ships on the [M5Stack CardputerZero](https://docs.m5stack.com/),
decoding **AIS** (Automatic Identification System) position reports the way `apps/adsb` shows
aircraft. It is a **port of the ADS-B app**: same shared toolkit (reactive MVVM shell, `EntityStore`,
SDL simulator / device backends), a different decoder and field mapping.

> Status (2026-06-26): the AIVDM decoder (message types 1/2/3) is implemented and **unit-tested against
> a two-source oracle** (gpsd's `gpsdecode` + `pyais`); the app builds green and lists decoded vessels.
> The radar/Mercator-map view (reusing `apps/adsb`'s `render_scope`) is the next step — see Follow-ups.

## The thesis: decode on a host, view on the device

Like ADS-B (which reads `dump1090`'s already-decoded `aircraft.json`), AIS keeps the **RF chain on a
host**: a tool such as [`rtl_ais`](https://github.com/dgiardini/rtl_ais) or **AIS-catcher** does the
GMSK 9600-bps demodulation, HDLC de-framing, CRC-16 and 6-bit ASCII armoring, and emits standard
`!AIVDM` NMEA sentences (two channels: 161.975 MHz / AIS 1 / 87B and 162.025 MHz / AIS 2 / 88B). The
device-side job — and the heart of this app — is to **decode each `!AIVDM` sentence** into a `Vessel`
and merge it into the shared `EntityStore`. This mirrors ADS-B exactly: a parser + a field mapping.

```
RTL-SDR --> rtl_ais (host: GMSK/HDLC/CRC/armor) --> !AIVDM lines --> [ais_app] parse_aivdm --> EntityStore --> view
            (== dump1090 for ADS-B)                                  (this repo)
```

## What this app contains

| Layer | File | Notes |
|---|---|---|
| **Decoder** | `src/decoder/ais_decoder.{h,cpp}` | `parse_aivdm()` + `Vessel`. Pure (std-only), no LVGL/toolkit, so the unit test links it standalone. The new code vs ADS-B's `parse_aircraft_json`. |
| Model map | `src/model/vessel_store.{h,cpp}` | `parse_nmea_lines()` + `apply_to_store()` — mirrors ADS-B's `apply_to_store` (sparse field bag keyed by MMSI). |
| Source | reuses `toolkit::FileJsonSource` | polls the NMEA file; the callback splits lines and decodes each. `AIS_NMEA` overrides the bundled mock. |
| ViewModel / View | `src/viewmodel/ais_viewmodel.*`, `src/view/ais_screen.*` | first cut: a vessel list (MMSI / position / SOG / COG) on the shared `BaseScreen`. |

## The decoder (message types 1/2/3)

`parse_aivdm()` decodes the **Common Navigation Block** (ITU-R M.1371-6, Table 46), 168 bits:

| Field | Bits | Decode |
|---|---|---|
| Message type | 0–5 | must be 1, 2 or 3 |
| MMSI | 8–37 (30) | unsigned |
| Nav status | 38–41 (4) | 0–15 (15 = not defined) |
| SOG | 50–59 (10) | ×0.1 kt; 1023 → not available |
| Longitude | 61–88 (28, **signed**) | two's-complement ÷ 600000 °; 181° → not available |
| Latitude | 89–115 (27, **signed**) | two's-complement ÷ 600000 °; 91° → not available |
| COG | 116–127 (12) | ×0.1 °; ≥3600 → not available |
| Heading | 128–136 (9) | 0–359; 360–510 reserved / 511 n/a → not available |

6-bit ASCII de-armoring: `v = c − 48; if (v > 40) v −= 8`, with the illegal `'X'`–`'_'` gap rejected.
The NMEA checksum (XOR between `!` and `*`) is verified; malformed/truncated/multi-fragment sentences
are rejected. "Not available" sentinels map to `has_*` flags (never a bogus 181°/1023 value), exactly
like ADS-B's `Aircraft`.

## Tests — the immutable oracle

`test/ais_decoder_test.cpp` is the reward verifier. Every expected value was produced by encoding a
field set with `pyais` and decoding the resulting `!AIVDM` with **both** `gpsdecode` **and** `pyais`,
requiring the two independent decoders to agree (`scratchpad/gen_oracle2.py`). The vectors cover the
classic decode traps:

- positive (N/E) and **negative (S/W)** coordinates → two's-complement on lat (27b) + lon (28b)
- SOG/COG scaling (1/10 kt, 1/10 °)
- **message type 3** (shared dispatch)
- the **not-available sentinels** (lon 181°, lat 91°, SOG 1023, COG 3600, HDG 511)
- a real-world canonical sentence (gpsd docs, negative longitude)
- across the set, both 6-bit de-armor branches; plus negative tests (truncated payload, armor-gap char)

```bash
cmake --preset linux-x86-64 && cmake --build --preset linux-x86-64-dbg
ctest --test-dir build/linux-x86-64 -C Debug --output-on-failure   # ais_decoder_test
```

## Run

```bash
./build/linux-x86-64/apps/ais/Debug/ais_app           # SDL window, bundled mock NMEA
AIS_NMEA=/path/to/stream.nmea ./build/.../ais_app      # live: point at rtl_ais output
```

To feed it live: run `rtl_ais` (or AIS-catcher) on the host and tee its `!AIVDM` output to a file the
app polls (or extend `nmea` ingestion to a TCP/UDP line source — see Follow-ups).

## Follow-ups

- **Radar / Mercator map view** — reuse `apps/adsb`'s `render_scope` + `toolkit::map::VectorMap` to
  draw vessels on the PPI and the world map (the list is the first cut).
- **More message types** — static/voyage data (type 5: name, callsign, dimensions), type 24, etc.
- **Live line source** — a TCP/UDP NMEA reader (rtl_ais can stream), instead of polling a file.
- **Hub entry** — add AIS to the `apps/radio` launcher catalog once the view is fleshed out.
