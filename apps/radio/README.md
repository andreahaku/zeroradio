# ZeroRadio hub — the launcher for the CardputerZero radio suite (`apps/radio`)

The hub is the entry point of **ZeroRadio 1.0.0** on the
[M5Stack CardputerZero](https://docs.m5stack.com/). Its title bar reads "ZeroRadio 1.0.0". It lists
the suite apps (**SDR**, **Scanner**, **ISM**, **ADS-B**, **AIS**, and **Meshtastic** when its
binary is present) plus an **About** row. When you pick an app, the hub hands the display to that
app's binary (a separate process) and takes it back when the app exits.

The hub `posix_spawn()`s the per-app binaries (`sdr_app`, `survey_app`, `ism_app`, `adsb_app`,
`ais_app`, `meshtastic_app`) instead of linking them into one program. Each app is an independent
process, so a crash in one cannot take the hub down, and a rebuild touches only the changed binary.

![Radio hub menu](docs/media/hub.png)

> The hub menu at native 320×170 (desktop SDL simulator): arrow-key navigation, Enter opens, Esc
> exits to the system launcher.

## One launcher entry

The APPLaunch home screen is a flat carousel with no categories. The suite therefore ships one
**ZeroRadio** entry (`applications/zeroradio.desktop`), and the hub provides the sub-menu.

The hub lists only the apps whose binary it finds. The 1.0.0 package ships SDR, Scanner, ISM,
ADS-B and AIS. It leaves out Meshtastic, which needs `meshtasticd` and the Cap LoRa hardware.

## Interaction

- **↑ / ↓** — move the selection (arrow keys, or nav keys `5` / `7`).
- **Enter / → / `6`** — open the highlighted app.
- **Esc** — leave the hub and return to the system launcher.

Inside an app, `Esc` returns to the hub. Holding `Esc` for 3 s returns straight to the system
launcher: the app exits with a "home" code, and the hub exits too.

The **About** row opens in the hub itself. It shows the version, the source link with a QR code,
the changelog (`CHANGELOG.md`) and the credits (`CREDITS.md`).

The menu captures keys through the toolkit's `platform::set_key_capture`, so it needs no NavBar.

## How the display hand-off works

The CardputerZero has a single framebuffer, and the desktop SDL window and the remote-fb port also
allow one owner. The hub and a child app cannot both hold it. The flow per launch:

1. The hub renders the menu in one LVGL session (`toolkit::run_app`).
2. On selection, the `run_app` teardown callback deletes the screen graph and releases the session:
   every indev, the remote-fb server (and its TCP port) and the display.
3. The hub `posix_spawn()`s the chosen binary and blocks in `waitpid()`, holding **no** display.
4. When the app exits, the hub **`execv()`s itself** to show the menu again.

The re-exec matters. LVGL and the SDL backend keep static state that a second `lv_init()` in the
same process cannot reset: the SDL event pump dies and the window freezes. A fresh process avoids
that. The device's fbdev/evdev path has no such state, but it uses the same flow.

An app can also hand the display to a sibling app directly (Scanner → SDR with `TAB` or **Open in
SDR**). The hub keeps waiting on the same process and shows the menu when that app exits.

## Binary resolution

`resolve_app_binary()` looks for each app's executable in this order:

1. `$RADIO_APPS_DIR/<bin>` — an explicit dev override.
2. `<hub_dir>/<bin>` — the install layout, with every app binary next to the hub in
   `/usr/share/zeroradio/bin/`.
3. `<hub_dir>/../../<app_dir>/<config>/<bin>` — the dev CMake build tree
   (`build/<preset>/apps/<app>/<config>/<bin>`), trying `Debug`, `Release` and `RelWithDebInfo`.
   The hub therefore runs from a desktop build with no setup.

The child inherits the environment, so `REMOTE_FB`, `ADSB_JSON`, `MESHTASTICD_PORT` and the other
variables pass straight through. On the device, the `.desktop` entry starts the hub through
`applications/radio-launch.sh`. That wrapper sets the SDR defaults (`SDR_SAMPLE_RATE=1024000`,
`SDR_ALSA_DEV=pipewire`) and then execs `radio_app`, so every child inherits them.

## Adding an app

Append an `AppEntry` to `app_catalog()` in `src/app_catalog.h` (`id`, `name`, `subtitle`, `icon`,
`bin`, `app_dir`). The icons are Phosphor-Fill glyphs from the toolkit's `ui_const.h`. The menu,
the resolution and the launch read this table, so no other change is needed.

## Build & run

Desktop SDL simulator:

```bash
cmake --preset linux-x86-64 && cmake --build --preset linux-x86-64-dbg --target radio_app
./build/linux-x86-64/apps/radio/Debug/radio_app    # SDL window; needs sibling apps built too
```

Build the whole suite first (`cmake --build --preset linux-x86-64-dbg`) so the hub finds the
sibling binaries in the dev tree. For headless dev over remote-fb, set `REMOTE_FB=<port>`. It passes
through to the launched app.

Device builds run in a Debian trixie container (preset `cp0-trixie`):

```bash
scripts/cp0-docker-build.sh --package   # all apps + the zeroradio .deb
```

The `.deb` installs to `/usr/share/zeroradio` and adds the APPLaunch entry and icon.

## Layout

```
apps/radio/
  applications/zeroradio.desktop # single launcher entry -> /usr/share/zeroradio/bin/radio-launch.sh
  applications/radio-launch.sh   # device launch wrapper: SDR env defaults, then exec radio_app
  src/
    main.cpp                     # outer flow: render menu -> spawn child -> re-exec
    app_catalog.h                # the suite, in menu order, plus the About row
    launch/app_launcher.{h,cpp}  # resolve binary + posix_spawn + waitpid
    viewmodel/hub_viewmodel.{h,cpp}
    view/hub_screen.{h,cpp}      # TitleBar + app list + footer hint, key capture, About viewer
```
