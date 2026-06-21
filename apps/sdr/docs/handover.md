# SDR app — handover / status

> The **SDR** app (`apps/sdr`) for the M5Stack CardputerZero (Linux ARM64, 320×170 RGB565, 5-key
> navigation `4-8` + `ESC`). Live **FFT spectrum + waterfall** and **real demodulated audio** from an
> RTL-SDR dongle. Ported from the standalone *SDRTerminal* onto the shared `radio_toolkit` in the
> cardputer-radio monorepo. Developed on the desktop SDL simulator at 320×170, fed by a real RTL-SDR
> over `rtl_tcp`.

References: [`README.md`](../README.md) (overview / features / how it works),
[`../../../docs/sdrterminal-port-plan.md`](../../../docs/sdrterminal-port-plan.md) (what was reused vs.
ported). Project memory: `sdr-app-hardware-test`.

---

## 1. Current status (DONE and VERIFIED on hardware)

- ✅ Desktop SDL build **green** as part of the monorepo (`cmake --build --preset linux-x86-64-dbg`).
- ✅ **Real RTL-SDR source** (`RtlTcpSource`): `rtl_tcp` client, IQ reader thread, 8192-point FFT
  (fftw3f), dB magnitudes with an adaptive noise floor → live spectrum + waterfall. Tuning/zoom retunes
  the hardware and crops the FFT window. Verified with an **RTL-SDR Blog V4** (R828D) on real FM broadcast.
- ✅ **Real audio** (`AudioDemod`, SDL 48 kHz mono): WFM/NFM/AM/SSB(Weaver)/CW. **WFM verified by ear.**
  NFM/AM/SSB/CW are correct in theory but **still to be verified** (antenna/noise, see §6).
- ✅ **5-page toolbar** (Tuning/Zoom/Visual/Audio/Settings) via the toolkit NavBar + `NavProvider`; key
  `4` cycles, `ESC` quits.
- ✅ **Audio controls** (page 4) and **gain** (page 5).
- ✅ **Full persistence** (state v4): VFO, mode, zoom, band, coarse/fine, dark, grids, peak, gain,
  volume, mute → reopens identical.
- ✅ Scrolling 1 s time marker, per-mode passband, peak hold, frequency grid, manual frequency-entry
  dialog, dark mode + grids on by default.

### Build & run (with a real RTL-SDR)
```bash
cd <repo root>/cardputer-radio
cmake --preset linux-x86-64                   # first time (downloads LVGL 9.5)
cmake --build --preset linux-x86-64-dbg
rtl_tcp -a 127.0.0.1 -p 1234 -s 2400000       # IQ server (separate terminal)
./build/linux-x86-64/apps/sdr/Debug/sdr_app   # connects to 127.0.0.1:1234
```
Overrides: `SDR_SOURCE=mock` (mock, no dongle), `SDR_RTLTCP=host:port` (remote rtl_tcp).

> **Dev gotcha:** the GUI **cannot be launched by the assistant** (the harness sandbox kills GUI launches
> with exit 144). The user launches with `!`; the assistant's proof is the green build, the visual/audio
> check is the user's. (memory `sdr-app-hardware-test`.)

---

## 2. Architecture (reactive MVVM + live data path)

```
input (5 keys + ESC) → key router (toolkit platform/linux_input)
   ├─ 4..8  → NavBar slot → NavProvider::nav_activate(page,slot) → SdrViewModel action → SdrModel + publish subject
   ├─ ESC   → quit handler (toolkit run_app)
   └─ capture → while the frequency dialog is open, every key goes to it

live data (independent of input):
   RTL-SDR ──rtl_tcp(IQ TCP)──▶ RtlTcpSource (bg thread)
                                   ├─ FFT (fftw3f) ─▶ shared magnitudes ─▶ next_frame() ─▶ chart + waterfall
                                   └─ AudioDemod ─▶ per-mode demod ─▶ SDL audio 48 kHz
   lv_timer (~33 ms) → SpectrumScreen::tick(): set_tuning/mode/volume/gain, pull a frame, draw
```

`SpectrumSource` is the UI's only dependency: `next_frame`, `set_tuning(center,span)`, `set_mode`,
`set_volume/set_muted`, `set_gain(auto,tenth_db)`. Implementations: `MockSpectrumSource`, `RtlTcpSource`.

---

## 3. 5-page NavBar (keys `4`-`8`)

| Key | P1 Tuning | P2 Zoom/Band | P3 Visual | P4 Audio | P5 Settings |
|---|---|---|---|---|---|
| `4` | page switch | switch | switch | switch | switch |
| `5` | tune − | zoom − | dark/light | mute | gain − |
| `6` | **freq entry** | band | freq grid | volume − | gain + |
| `7` | tune + | zoom + | time grid (1 s) | volume + | gain auto/value |
| `8` | coarse/fine (step) | **mode** | peak hold | volume % | exit |

Driven by `SdrViewModel::nav_fill`/`nav_activate` (slot 0 / key 4 is the page switcher, owned by the
toolkit shell). Phosphor icons with codepoints verified by rendering the font.

---

## 4. SDR-specific files (`apps/sdr/src/`)

```
sdr/spectrum_source.{h,cpp}   # SpectrumSource interface + MockSpectrumSource
sdr/rtl_tcp_source.{h,cpp}    # RtlTcpSource: rtl_tcp client + FFT fftw3f (pImpl, gated by SDR_HAVE_RTLTCP)
sdr/audio_demod.{h,cpp}       # AudioDemod: decimating FIRs + FM/AM/Weaver + AGC + SDL output
view/spectrum_screen.{h,cpp}  # header/chart/waterfall/passband/grids/time-marker/freq dialog (on BaseScreen)
model/sdr_model.{h,cpp}       # state (VFO Hz, mode, span, bands, audio, gain) + v4 persistence
viewmodel/sdr_viewmodel.{h,cpp} # subjects + actions, bridging ShellViewModel + NavProvider
```
CMake (`apps/sdr/CMakeLists.txt`): links `radio_toolkit`; `fftw3f` + `SDL2` + `Threads` enable the real
source (desktop-gated by `SDR_HAVE_RTLTCP`). The cross build stays on mock until fftw3f is in the BSP.

---

## 5. State (Model) and persistence

- VFO in **Hz** (int32, clamped to [0, INT32_MAX]); display 4 decimals (header) / 3 decimals (waterfall labels).
- `RadioMode` WFM/FM/AM/USB/LSB/CW; 5 tool pages; span ladder `{2000,1500,1000,500,200,100}` kHz
  (zoom 0.1–2.0 MHz); band presets FM/Air/2m/70cm; audio (volume 0-100, muted); gain (auto + tenth_db).
- Persistence in `$XDG_CONFIG_HOME/cardputer_radio/sdr/state` (fallback `$HOME/.config/...`) via the
  toolkit `config_file` helper, **version v4**: saves the whole session on every action; load validates
  ranges; legacy formats are ignored once.

---

## 6. Known limits / to verify

- **NFM/AM/SSB/CW audio not verified by ear**: with a short dipole and heavy RF noise, only WFM comes
  through well. The demod code is correct but needs a better antenna/location or a networked dongle.
- 3-decimal frequency on the waterfall labels = 1 kHz display resolution (100 Hz fine tuning won't move
  there; the header stays at 4 decimals).
- Real source is **desktop-only** (cross build uses the mock).

---

## 7. Remaining work

- **Verify NFM/AM/SSB/CW** with a decent antenna (see §6).
- **Settings page**: besides gain, possibly sample rate / bias-tee / ppm.
- **Device**: cross-build `.deb` once hardware arrives; provision `fftw3f` in the BSP for the real source.
- **Networked source** (remote rtl_tcp tap): the TCP boundary is already in place.

---

*Status 2026-06-21. Build green; real RTL-SDR source, audio (WFM), 5-page controls and full persistence
verified on the SDL simulator with an RTL-SDR Blog V4.*
