# remote-fb — Path B: run the app on the device, see/drive it from the desktop

Run a cardputer-radio app **headless on the device** (e.g. a Raspberry Pi Zero 2 W
with the RTL-SDR attached) and **view + drive its 320x170 screen from the X1** over
the network, before the physical SPI display arrives.

```
 device (Pi)                                   desktop (X1)
┌───────────────────────────┐                 ┌──────────────────────────┐
│ RTL-SDR ─ decoder ─ APP    │   framebuffer   │  remote-fb-viewer (SDL2) │
│ (LVGL, headless)           │ ──────────────► │  shows 320x170, scaled   │
│ remote_fb display + keypad │ ◄────────────── │  forwards your keystrokes │
└───────────────────────────┘   key events     └──────────────────────────┘
```

The app renders with no physical screen and streams each flushed region over TCP;
the viewer blits it and sends keys back, so the remote app is fully drivable from
the X1 keyboard (the Cardputer's 5 keys: `4 5 6 7 8`, plus `Esc`, digits, `.`).

## Files

| File | Role |
| --- | --- |
| `remote_fb_proto.h` | Wire protocol (HELLO / FRAME / KEY), shared by both ends. |
| `viewer.cpp` → `remote-fb-viewer` | Desktop SDL2 viewer + key forwarder. |
| `mock_streamer.cpp` → `remote-fb-mock` | Test pattern source (no LVGL), to exercise the viewer without a Pi. |
| `selftest.cpp` → `remote-fb-selftest` | Headless protocol check (CI / quick verify). |
| `demo.sh` | One-command: run an app headless + open the viewer. |
| `../../toolkit/src/platform/remote_fb.{h,cpp}` | **Device side**: the headless streaming LVGL display + keypad indev. Enabled by the `REMOTE_FB` env var. |

## Build

```bash
# viewer + mock + selftest (desktop tools)
cmake -S tools/remote-fb -B tools/remote-fb/build -DCMAKE_BUILD_TYPE=Release
cmake --build tools/remote-fb/build

# the apps (the remote_fb driver is compiled into the toolkit automatically)
cmake --preset linux-x86-64 && cmake --build --preset linux-x86-64-dbg
```

## Run

```bash
# easiest: one command (ADS-B, mock data — no dongle needed)
./tools/remote-fb/demo.sh

# manual, equivalent:
REMOTE_FB=5800 ./build/linux-x86-64/apps/adsb/Debug/adsb_app &   # app = server
./tools/remote-fb/build/remote-fb-viewer 127.0.0.1 5800 1        # viewer (scale 1)
```

`REMOTE_FB=<port>` (or `REMOTE_FB=1` for the default 5800) switches **any** build —
desktop or device — into headless streaming mode, so the same binary runs on the
Pi exactly as it does here.

### Real RTL-SDR data (not the bundled mock)

```bash
# SDR spectrum/waterfall — real RF
rtl_tcp -a 127.0.0.1
REMOTE_FB_PORT=5800 ./tools/remote-fb/demo.sh sdr     # (SDR_RTLTCP=host:port to override)

# ADS-B — real aircraft (needs a 1090 antenna + traffic overhead)
dump1090 --net --write-json /tmp/dump1090 --write-json-every 1 --lat <LAT> --lon <LON>
ADSB_JSON=/tmp/dump1090/aircraft.json REMOTE_FB_PORT=5800 ./tools/remote-fb/demo.sh adsb
```

## Verify without a Pi or a display

```bash
./tools/remote-fb/build/remote-fb-mock 5800 &
./tools/remote-fb/build/remote-fb-selftest 127.0.0.1 5800   # prints PASS
```

## Protocol notes

- App is the **server** (binds the port); the viewer is the **client** and may
  reconnect freely. On each connection the app sends `HELLO` then a full frame.
- Wire pixel format is **RGB565** (the SPI panel's native format → zero-copy on the
  device 16-bit build; the desktop 32-bit build converts in the flush). One full
  frame is ~108 KB; partial flushes send only the dirty region.
- Both ends are little-endian; `HELLO` carries a magic+version to catch mismatches.
- Keys are LVGL key codes (`'4'`-`'8'`, `LV_KEY_ESC`, digits, `.`, enter, backspace),
  fed into a keypad indev on the device via the same `attach_key_router()` path the
  physical Cardputer keys use.

## Future work (tracked)

- **Auto-start the SDR background processes.** Today `rtl_tcp` / `dump1090` /
  `AIS-catcher` must be launched by hand before the app. The app (or a small
  supervisor) should start the right decoder for the selected mode automatically.
- **Dependency check / bundling / auto-download.** On startup, verify the required
  tools (rtl-sdr ≥ 2.0, dump1090, AIS-catcher) are present; bundle them with the
  app image or fetch/build them on first run if missing, so the device is
  plug-and-play.
- **Emulator integration (optional).** Feed the streamed framebuffer into the
  CardputerZero emulator window instead of the standalone viewer.
