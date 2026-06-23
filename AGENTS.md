# AGENTS.md — cardputer-radio

Context for any coding session working in this repo (auto-loaded by Claude Code and
Pi). Read this first, then the relevant per-app `README.md` and `docs/architecture.md`.

## What this is

A family of radio apps for the **M5Stack CardputerZero** (a Pi-class Linux ARM64
handheld, 320x170 RGB565 screen, 5 keys). One shared **toolkit** (LVGL MVVM,
reactive subjects, 5-key NavBar, resilient TCP/HTTP sources) + thin per-app
viewers. Unifying thesis: **decode on a host, view on the device** — each app is a
parser + a field mapping over the shared toolkit.

- `toolkit/` — the reusable library (app shell, reactive, platform, view, net, geo, model).
- `apps/adsb` — ADS-B aircraft radar/list (data: dump1090 `aircraft.json`, mock bundled).
- `apps/sdr`  — SDR spectrum/waterfall + audio (data: `rtl_tcp`, mock synthetic).
- Design docs for the wider suite: `../radio-apps/`.

## Build & run

```bash
# desktop (X1) — LVGL SDL simulator
cmake --preset linux-x86-64 && cmake --build --preset linux-x86-64-dbg
./build/linux-x86-64/apps/adsb/Debug/adsb_app      # SDL window

# device (CardputerZero cross build) — needs the BSP sysroot
cmake --preset cp0-cross && cmake --build --preset cp0-cross-rel
```

The LVGL display backend is chosen in `toolkit/src/app/run_app.cpp` `init_display()`:
SDL (desktop) / DRM / fbdev (device) / **remote-fb** (headless streaming, see below).

## Feature: remote-fb (Path B) — run on the device, view/drive from the desktop

**Purpose.** Run an app **headless on the device** (e.g. a Raspberry Pi Zero 2 W
with the RTL-SDR) and **see + drive its 320x170 screen from the X1** over TCP,
before the physical SPI display arrives. Full docs + protocol:
[`tools/remote-fb/README.md`](tools/remote-fb/README.md).

**How it works.** The app renders headless and streams each flushed framebuffer
region over TCP (app = server); a desktop SDL2 viewer blits it and forwards
keystrokes back into a keypad indev (same `attach_key_router()` path as the
physical keys). Wire format RGB565; protocol in `tools/remote-fb/remote_fb_proto.h`.

**Enable it** on ANY build with the env var:

```bash
REMOTE_FB=<port>   # or REMOTE_FB=1 for the default 5800
```

`init_display()` honours it (`toolkit/src/app/run_app.cpp`); the driver is
`toolkit/src/platform/remote_fb.{h,cpp}` (compiled into the toolkit automatically).

**Run it (one command):**

```bash
./tools/remote-fb/demo.sh            # ADS-B, mock data, no dongle
./tools/remote-fb/demo.sh sdr        # SDR app
# viewer scale: REMOTE_FB_SCALE=2|3 (default 1 = real panel size)
```

**Real RTL-SDR data** (the apps default to mock):

```bash
rtl_tcp -a 127.0.0.1 ;            REMOTE_FB_PORT=5800 ./tools/remote-fb/demo.sh sdr
dump1090 --net --write-json /tmp/dump1090 --write-json-every 1 --lat <LAT> --lon <LON>
ADSB_JSON=/tmp/dump1090/aircraft.json REMOTE_FB_PORT=5800 ./tools/remote-fb/demo.sh adsb
```

**Develop with no Pi / no display:** `remote-fb-mock` (test pattern) +
`remote-fb-selftest` (headless protocol check) under `tools/remote-fb/`.

**Critical gotcha (already handled — don't regress).** Headless mode skips the
SDL/driver init that normally registers LVGL's tick + delay source, so without one
LVGL's clock never advances (timers freeze, only the first frame renders).
`remote_fb_create()` installs `lv_tick_set_cb` / `lv_delay_set_cb` itself. Any new
headless/no-backend path must do the same.

**Open future-work** (see the README's Future work): auto-start the RTL-SDR
background processes (rtl_tcp/dump1090/AIS-catcher), verify/bundle/auto-download
dependencies, optional emulator-window integration of the stream.

## Conventions

- Commit messages: plain Conventional Commits.
- Keep the desktop build green; the `remote_fb` driver compiles on desktop too.
- Match existing patterns (MVVM reactive subjects, the toolkit's source-over-a-TCP
  boundary) — a new app should be mostly a parser + a field mapping.
