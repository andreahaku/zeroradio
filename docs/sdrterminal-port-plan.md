# Porting plan — SDRTerminal → cardputer-radio monorepo

> **STATUS: COMPLETE (2026-06-21).** SDRTerminal is fully ported to `apps/sdr` at feature parity, building
> green on the desktop sim (mock source always; live RTL-SDR via rtl_tcp when fftw3f + SDL2 are present).
> Ported: SdrModel (radio state + persistence), SdrViewModel (5-page NavBar via NavProvider), SpectrumScreen
> (chart/waterfall/S-meter/grids/passband/freq-dialog), spectrum_source (+mock), rtl_tcp_source, audio_demod.
> Reused from the toolkit: shell/run_app, NavBar/widgets, BaseScreen, reactive, logger, platform, theme,
> asset_manager + new config-path helpers. `app.cpp`/`screen_manager` were intentionally dropped (single
> screen → `run_app` + `build_root`); `screen_manager` will be generalized into the toolkit when a genuinely
> multi-screen app needs it (no consumer today).


> Date: 2026-06-21. Goal: bring the existing **SDRTerminal** (LVGL SDR receiver) into this monorepo as
> `apps/sdr/`, reusing the shared `radio_toolkit` library. Based on a full read-only map of
> `../SDRTerminal` (24 files, ~6.3k LOC). Source repo stays untouched; we copy + adapt into `apps/sdr/`.

## Key insight
`radio_toolkit` **was extracted from SDRTerminal**, so the "common parts" already live here in their
*generalized* form (decoupled via `ShellViewModel` + `NavProvider`). SDRTerminal still has the *old coupled*
versions. So the port is mostly:
1. **Delete** SDRTerminal's duplicated shell and point at the toolkit.
2. **Refactor** its coupled `BaseViewModel`/`NavBar`/`BaseScreen` usage onto the toolkit's decoupled pattern
   (exactly what `AdsbViewModel`/`AdsbScreen` already demonstrate).
3. **Move** the genuinely SDR-only code under `apps/sdr/`.

The SDR NavBar (5 toolbar pages, dynamic slot text) maps **cleanly** onto the toolkit `NavProvider`
(`nav_page_count=5`, per-page `nav_fill`, `nav_activate`) — and slot 0 = page switcher is exactly the
toolkit/SDRTerminal convention the user confirmed (key 4 always cycles the page). The frequency dialog's
key capture already exists in the toolkit (`platform::set_key_capture`).

## Reuse vs. new (from the map)

### Already in the toolkit — reuse as-is (byte-identical in SDRTerminal)
`reactive/{subjects,bindings}`, `logger/`, `view/theme`, `view/ui_const.h`, `app/asset_manager`,
`view/widgets/base_widget.h`, `platform/linux_input` (interface incl. `set_key_capture`). Plus the
generalized `run_app`, `navbar`, `icon_button`, `titlebar`, `base_screen`, `shell_viewmodel`, `nav_provider`.

### SDR-only — port into `apps/sdr/` (write/adapt)
| Source (SDRTerminal) | Target | Notes |
|---|---|---|
| `model/base_model.*` | `apps/sdr/src/model/sdr_model.*` | Radio state machine: VFO, mode (WFM/FM/AM/USB/LSB/CW), span ladder, gain, band presets, state persistence (`$XDG_CONFIG_HOME/sdrterminal/state`). No shell coupling → near-verbatim. |
| `viewmodel/base_viewmodel.*` | `apps/sdr/src/viewmodel/sdr_viewmodel.*` | **Biggest refactor.** Derive from `toolkit::ShellViewModel` + implement `toolkit::NavProvider`; keep the ~16 SDR subjects + tune/zoom/mode/audio/gain actions; move the 5-page NavBar logic into `nav_fill`/`nav_activate` (dynamic slot text: tuning step "25k", gain "29d/auto"). |
| `view/screens/spectrum_screen.*` | `apps/sdr/src/view/spectrum_screen.*` | **820 LOC.** FFT line chart + RGB565 waterfall canvas + S-meter + passband/grids + frequency dialog. Derive from `toolkit::BaseScreen` like `AdsbScreen`. **Re-fit layout to 320×170** (see risks). |
| `view/sdr/spectrum_source.*` | `apps/sdr/src/sdr/spectrum_source.*` | Abstract `SpectrumSource` + `MockSpectrumSource` (no extra deps — port first). |
| `view/sdr/rtl_tcp_source.*` | `apps/sdr/src/sdr/rtl_tcp_source.*` | RTL-SDR TCP client + FFTW3f 8192-pt FFT, pImpl + background thread. Gate behind `SDR_HAVE_RTLTCP`. |
| `view/sdr/audio_demod.*` | `apps/sdr/src/sdr/audio_demod.*` | Baseband demod → SDL audio queue. |
| `app/app.*` + `app/screen_manager.*` | dropped → `apps/sdr/src/main.cpp` | SDR has one screen → use `toolkit::run_app` + a `build_root` lambda (like `apps/adsb/src/main.cpp`). Generalize `screen_manager` into the toolkit only when a multi-screen app needs it. |

## Refactor specifics
- **`SdrViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider`** — mirror `AdsbViewModel`.
  - `nav_page_count() = 5`; pages = Tuning / Zoom / Visual / Audio / Settings.
  - `nav_fill(page, out[5])`: slot 0 ignored (page number), slots 1..4 per the existing SDR table, with
    dynamic text computed from the model (step, gain, mute icon).
  - `nav_activate(page, slot)`: dispatch to `tune_up/down`, `zoom_in/out`, `cycle_band/mode`,
    `toggle_*`, `volume_*`, `gain_*`, `request_freq_input`, `request_quit`.
- **`SpectrumScreen : public screen::BaseScreen`** — ctor `(SdrViewModel&, AssetManager&, …)`; keep the
  30 Hz `tick()` pipeline (source → chart + waterfall + S-meter); freq dialog via `set_key_capture`.
- The SDR subjects currently read by `navbar`/`titlebar` move into `SdrViewModel`; the toolkit widgets
  already take `lv_subject_t*`, so binding is mechanical.

## CMake
- New `apps/sdr/CMakeLists.txt`: glob `src/`, link `radio_toolkit` + `Threads::Threads` + `fftw3f`
  (optional, gates `SDR_HAVE_RTLTCP`) + SDL2 audio. Add `add_subdirectory(apps/sdr)` to the top-level.
- Fix the open review item while here: **link `Threads::Threads` to `radio_toolkit`** (it already uses
  `std::thread` via `file_json_source`) so threading flags are correct on every toolchain.
- Reuse the existing `lv_conf*.h`; no change needed.

## Incremental sequence (build green at each step)
1. Scaffold `apps/sdr/` + CMake linking the toolkit; a stub `SpectrumScreen` that builds and shows an
   empty body. **Build.**
2. Port `sdr_model` (state + persistence) — no shell coupling. **Build.**
3. `SdrViewModel : ShellViewModel + NavProvider` with the 5 pages wired (dynamic slot text). **Build.**
4. Port `MockSpectrumSource` + the chart/waterfall rendering onto `BaseScreen`, **re-fit to 320×170**.
   Runs on mock, no fftw/rtl deps. **Build + user launches GUI.**
5. Port the frequency dialog (reuse `set_key_capture`). **Build.**
6. `main.cpp` via `run_app` + `build_root`. **Build.**
7. Add `RtlTcpSource` + `AudioDemod` behind `SDR_HAVE_RTLTCP` (fftw3f, Threads, SDL audio). **Build.**

## Open decisions / risks (need user input)
- **App dir name:** `apps/sdr/` (recommended) vs `apps/sdrterminal/`.
- **Screen re-fit:** SDRTerminal's spectrum layout (e.g. 120px waterfall) likely assumed a taller screen;
  the target here is **320×170** (body ≈ 110–140px). The waterfall/chart heights must be re-fitted — same
  class of fix as the ADS-B PPI clipping. Confirm the device is 320×170 for SDR too.
- **Generalize now or later?** Keep `sdr_model` and `screen_manager` app-local for now; promote to the
  toolkit only when a 3rd app needs them (avoids premature abstraction). Agree?
- **Deps in the monorepo build:** OK to add `fftw3f` + SDL2 audio (gated) to the build? RTL-SDR path needs
  a running `rtl_tcp`; the mock source needs neither.
- **State file path:** keep `sdrterminal/state` or rename to `sdr/`? (Affects upgrade continuity.)

## Not in scope (yet)
Cross-build (`cp0-cross`), `.deb` packaging, real on-device RTL-SDR validation — same staging as ADS-B
(device milestone in Nov).
