# ISM — 433/868 MHz device sniffer for the M5Stack CardputerZero (`apps/ism`)

The **ISM** app is part of **ZeroRadio 1.0.0**. It lists nearby ISM-band transmissions (weather
sensors, **TPMS** tyre-pressure monitors, remotes, energy meters) decoded by
[`rtl_433`](https://github.com/merbanan/rtl_433). ISM devices report no position, so the app has no
radar: its screens are **List / Detail / Settings**. It shares the toolkit with the ADS-B app
(reactive MVVM shell, `EntityStore`, SDL simulator / device backends) and adds its own decoder and
field mapping.

![ISM device list — live TPMS off-air](docs/media/live.png)

> The List at native 320×170 (desktop SDL simulator), driven by a real **RTL-SDR Blog V4** at
> 433.92 MHz: two car TPMS decoded off-air as they passed. Key `8` locks a device for the Detail
> screen. The source dot (top-right) turns green once `rtl_433` decodes.

## Features

- **Pure decoder** — `parse_ism_json_line` (`src/model/ism_reading`) turns one `rtl_433 -F json`
  line into an `IsmReading`: model, id, channel, type, temperature, humidity, pressure, battery,
  RSSI, SNR, frequency, and any unmapped scalar fields as `extras`. A stable `device_key()`
  (`model[/id][@channel]`) identifies each device. The decoder converts pressure to kPa
  (`pressure_PSI` included). It builds as the `ism_decoder` static lib, and a **frozen two-source
  parity test** covers it (`test/ism_parse_test.cpp`, CTest target `ism_parse_test`). A python
  oracle (`test/gen_ism_vectors.py`) implements the documented contract on its own. The vectors
  include **real transmissions captured off-air** with the V4 (`test/ism_capture.jsonl`).
- **Live source** — `IsmSource` (`src/source/`) starts `rtl_433 -F json` on a worker thread through
  the shared `toolkit::Subprocess` and stops it on exit. It buffers partial lines, decodes each line
  into the `EntityStore`, and respawns the tool with backoff if it exits.
- **List** — a themed table (MODEL / TYPE / AGE) with a green cursor band. A dot marks the locked
  device. Key `5` sorts by MODEL, AGE or RSSI. The title shows the live device count.
- **Detail** — key `8` locks the cursor device. The Detail screen shows every reported field with
  units, the `extras`, and the last-heard age.
- **Settings** — a 2-column name|value table (Theme, TTL). Key `7` cycles the focused value. The
  app saves it to `~/.config/zeroradio/ism/settings`.

Global keys, shared by every ZeroRadio app:

- `Esc` — back to the hub. Hold `Esc` for 3 s to return to the system launcher.
- `H` — open the in-app help ([`docs/help/ism.md`](../../docs/help/ism.md)).
- `F`/`X` (up/down) — move the List, Detail and Settings cursors.

## Screenshots

| List | Detail | Settings |
| --- | --- | --- |
| ![list](docs/media/list.png) | ![detail](docs/media/detail.png) | ![settings](docs/media/settings.png) |

The List/Detail/Settings above use the built-in mock source. The banner and the live Detail below
come from a real RTL-SDR V4 at 433.92 MHz.

| Live Detail — real Abarth TPMS (PSI→kPa, extras) |
| --- |
| ![live detail](docs/media/live-detail.png) |

## Run

On the [CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero), plug the RTL-SDR into the USB-A port and open **ISM** from the ZeroRadio hub.
The `zeroradio` `.deb` pulls in `rtl_433`.

On the desktop SDL simulator:

```bash
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg --target ism_app
./build/linux-x86-64/apps/ism/Debug/ism_app          # starts `rtl_433 -F json` on a local dongle
```

Device builds use `scripts/cp0-docker-build.sh` (Debian trixie container, preset `cp0-trixie`).
`scripts/cp0-docker-build.sh --package` produces the `.deb`, which installs to `/usr/share/zeroradio`.

Environment:

- `ISM_SOURCE=mock` — synthetic readings (a weather sensor and two TPMS), with no tool or hardware.
- `ISM_JSON=/path/stream.jsonl` — replay newline-delimited `rtl_433` JSON from a file.
- `ISM_SOURCE=rtl_tcp:<host>:<port>` — read a dongle on another machine (`rtl_433 -d rtl_tcp:host:port`).
- `ISM_RTL433_ARGS="-f 868M"` — extra args for the `rtl_433` command (e.g. tune 868 MHz).

`rtl_433` opens the dongle directly. Do not run a second `rtl_433` or `rtl_power` against the same
dongle at once. On a Linux desktop, blacklist the kernel DVB driver (`dvb_usb_rtl28xxu`) so it does
not grab the dongle (see the root README).

## Testing

```bash
ctest --test-dir build/linux-x86-64 -C Debug -R ism_parse_test
```

The parser test is the frozen reward: nobody edits it to make code pass. To add a new captured line
to `test/ism_capture.jsonl`, regenerate the vectors with `python3 test/gen_ism_vectors.py`.

## License

MIT. See the repo `assets/` folder for third-party asset license notes.
