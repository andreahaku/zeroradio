# remote-fb — view and drive an app's screen from the desktop

A development tool. It runs a ZeroRadio app **headless** and streams its 320×170 screen over TCP to
a desktop viewer, which sends your keystrokes back. Use it to watch and drive an app on the device
from a desktop keyboard, or to take screenshots and demos from a desktop build.

```
 device or desktop                             desktop
┌────────────────────────────┐                 ┌───────────────────────────┐
│ RTL-SDR ─ decoder ─ APP     │   framebuffer   │  remote-fb-viewer (SDL2)  │
│ (LVGL, headless)            │ ──────────────► │  shows 320x170, scaled    │
│ remote_fb display + keypad  │ ◄────────────── │  forwards your keystrokes │
└────────────────────────────┘   key events     └───────────────────────────┘
```

The app renders with no physical screen and streams each flushed region. The viewer blits it and
forwards keys, so you can drive the remote app from the desktop keyboard.

## Files

| File | Role |
| --- | --- |
| `remote_fb_proto.h` | Wire protocol (HELLO / FRAME / KEY), shared by both ends. |
| `viewer.cpp` → `remote-fb-viewer` | Desktop SDL2 viewer + key forwarder. |
| `mock_streamer.cpp` → `remote-fb-mock` | Test-pattern source (no LVGL), to exercise the viewer without an app. |
| `selftest.cpp` → `remote-fb-selftest` | Headless protocol check (CI / quick verify). |
| `demo.sh` | One command: run an app headless and open the viewer. |
| `../../toolkit/src/platform/remote_fb.{h,cpp}` | **App side**: the headless streaming LVGL display + keypad input. The `REMOTE_FB` environment variable enables it. |

## Build

```bash
# viewer + mock + selftest (desktop tools)
cmake -S tools/remote-fb -B tools/remote-fb/build -DCMAKE_BUILD_TYPE=Release
cmake --build tools/remote-fb/build

# the apps (the toolkit always includes the remote_fb driver)
cmake --preset linux-x86-64 && cmake --build --preset linux-x86-64-dbg
```

## Run

```bash
# one command: ADS-B with the bundled sample data (no dongle needed)
ADSB_SOURCE=mock ./tools/remote-fb/demo.sh

# the same, by hand:
REMOTE_FB=5800 ADSB_SOURCE=mock ./build/linux-x86-64/apps/adsb/Debug/adsb_app &   # app = server
./tools/remote-fb/build/remote-fb-viewer 127.0.0.1 5800 1                          # viewer (scale 1)
```

`demo.sh [app]` takes the app directory name (`adsb` by default, or `sdr`, `ais`, `ism`, `survey`).
It reads `REMOTE_FB_PORT` (default 5800) and `REMOTE_FB_SCALE` (default 1), and passes the rest of
the environment to the app.

`REMOTE_FB=<port>` (or `REMOTE_FB=1` for port 5800) switches **any** build, desktop or device, into
headless streaming mode. To view an app running on the device, start it there with `REMOTE_FB=5800`
and point the viewer at the device's address.

### Real RF

With a dongle attached, each app starts its own decoder as it does on the device (`rtl_tcp`,
`readsb`, `AIS-catcher`, `rtl_433`, `rtl_power`). The decoder must be installed or bundled next to
the app binary.

```bash
./tools/remote-fb/demo.sh sdr                             # starts rtl_tcp for the local dongle
SDR_RTLTCP=<host>:1234 ./tools/remote-fb/demo.sh sdr      # uses an existing rtl_tcp server instead
```

## Verify without an app or a display

```bash
./tools/remote-fb/build/remote-fb-mock 5800 &
./tools/remote-fb/build/remote-fb-selftest 127.0.0.1 5800   # prints PASS
```

## Protocol notes

- The app is the **server** and binds the port. The viewer is the **client** and may reconnect at
  any time. On each connection the app sends `HELLO`, then a full frame.
- The wire pixel format is **RGB565**, the panel's native format: the 16-bit device build sends its
  buffer as is, and the 32-bit desktop build converts in the flush. A full frame is ~108 KB. Partial
  flushes send only the dirty region.
- Both ends are little-endian. `HELLO` carries a magic and a version to catch mismatches.
- Keys travel as LVGL key codes. On the app side they enter a keypad input device through the same
  `attach_key_router()` path as the physical keys.
- The viewer forwards `4`-`8` (the NavBar), the other digits, `.`, Esc, Enter and Backspace, each
  as a press and a release. It does not forward TAB, H, letters or arrows.
- A new headless path must install its own LVGL tick and delay callbacks, as `remote_fb_create()`
  does.

## Future work

- **Forward the remaining keys** (TAB, H, F/X/Z/C, arrows) so every app action works remotely.
- **Emulator integration (optional).** Feed the streamed framebuffer into the CardputerZero emulator
  window instead of the standalone viewer.
