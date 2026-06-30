# SDR on-device profile — CardputerZero (CM0) @ 1.024 Msps

Date: 2026-06-30. First real CPU/RAM/thermal profile of `sdr_app` on the physical device.

## Setup

- **Device**: Raspberry Pi Compute Module 0, 4× Cortex-A (pinned 1.0 GHz), 415 MB RAM,
  kernel `6.12.75+rpt-rpi-v8`, Debian trixie. APPLaunch running in the background (realistic).
- **Capture model**: dongle on the host (X1), not on the device — the USB host port is still
  hardware-blocked (see `device-usb-host-blocker`). RTL-SDR Blog **V4** on the X1 →
  `rtl_tcp -a 0.0.0.0 -p 1234` → device connects over LAN.
- **Run env**: `SDR_RTLTCP=<device-ip>:1234`, `SDR_SAMPLE_RATE=1024000`. Rendering to `/dev/fb0`.
- **Audio**: NOT engaged — blocked by PipeWire (see finding below). The profile is DSP + display only.
- Profiler: `scripts/sdr-profile.sh 30 sdr_app` (per-core from `/proc/stat`, no `mpstat` on device).

## Results

| Metric | Value | Verdict |
|--------|-------|---------|
| `sdr_app` CPU | 90–150 %, median **~118 %** → **~1.2 of 4 cores** | ~30 % of quad-core capacity |
| Per-core busy (system, under load) | cpu0 47 % · cpu1 40 % · cpu2 19 % · cpu3 42 % (~37 % avg) | multi-threaded DSP, spread; ~2.5 cores idle |
| RAM (RSS) | **~9.6 MB**; MemAvailable 173–201 MB steady | negligible — the 415 MB cap is a non-issue for SDR |
| Temperature | **59–60 °C** steady | well within limits |
| Throttling | `get_throttled = 0x0` throughout; ARM clock pinned at 1.0 GHz | no thermal/voltage cap |
| Swap | 99 MB used (pre-existing, no pressure during run) | unrelated to SDR |

**Conclusion**: at 1.024 Msps with waterfall + demod, the SDR costs **~1.2 cores, <10 MB RAM, 60 °C,
zero throttling** on the CM0. Ample headroom.

## Caveat — local-dongle delta (not yet measured)

This profile offloads the **USB capture** cost to the X1's `rtl_tcp`. With the dongle attached
directly to the device (once the USB host blocker is cleared), the device additionally pays for
libusb/librtlsdr draining IQ at 1.024 Msps — typically **~0.2–0.4 of a core** on a Pi. Estimated
local-attach total: **~1.4–1.6 cores**, still against ~2.5 free. To turn the estimate into a number,
re-run with `rtl_tcp -a 127.0.0.1` on the device (loopback USB capture cost equals direct-attach cost).

**Mitigation available**: CPU affinity (`taskset`) can dedicate core(s) to the SDR pipeline, isolating
the capture+DSP load from the launcher/UI and the rest of the system.

## Audio — diagnosed and RESOLVED (2026-06-30)

On the real device image the audio stack is **PipeWire**, which holds the ES8388 codec
**exclusively** (`card 1` reports `0/1` free subdevices). Out of the box the **`pipewire-alsa`
bridge was not installed** (no `pipewire` PCM in `aplay -L`), so `sdr_app`'s direct ALSA output
failed on every target: `plughw:1,0` → *Device busy*, `default` → *ENOTSUPP (524)*, `dmix`/`hw`
hit the same exclusive-hold. This contradicted the previously recorded `plughw:1,0` working
default — audio was never actually verified end-to-end on this PipeWire image.

**Fix applied:**
1. Install the bridge: `apt-get install pipewire-alsa` (exposes the `pipewire` ALSA PCM).
2. Route the SDR sink through it: `SDR_ALSA_DEV=pipewire` (PCM via PipeWire; the hardware mixer
   still targets `hw:1` via `SDR_ALSA_CARD`, and `controlC1` is shareable with wireplumber).

Verified: `wpctl status` showed `sdr_app` as a PipeWire client with `output_FL/FR →
bcm2835-i2s-ES8389 HiFi playback [active]`, and audio was **confirmed audible** on the device speaker.

When launched by APPLaunch as user `pi`, `XDG_RUNTIME_DIR=/run/user/1000` is already set, so the
pipewire ALSA plugin connects to the session daemon automatically — no extra env needed.

**Repo wiring** (so a fresh device works): `apps/radio/applications/radio-launch.sh` exports
`SDR_ALSA_DEV=pipewire`; the `.deb` must `Depends: pipewire-alsa`. A future option is native
PipeWire output (`pw-stream`) to drop the ALSA-bridge dependency entirely.

**Amp-pop gotcha (fixed 2026-06-30):** `pipewire-alsa` also installs `99-pipewire-default.conf`, which
makes PipeWire the ALSA **`default`**. Something at codec init (APPLaunch?) then opens `default` →
powers the ES8388 speaker amp (`hpamp-regulator`) → **a few seconds of noise on every boot / APPLaunch
restart** (before pipewire-alsa, opening `default` hit the busy hw and silently failed → no pop). Fix:
disable that one file (`mv .../99-pipewire-default.conf{,.disabled}`); SDR audio is unaffected because it
uses the explicit `pipewire` PCM from `50-pipewire.conf`. **This is a distribution blocker**: a `.deb`
that `Depends: pipewire-alsa` reintroduces 99-default + the pop on a clean device. Resolve by shipping an
override that neutralises 99-default, or — cleaner — give the app **native PipeWire output (pw-stream)**
and drop the ALSA-bridge dependency. (The ADC→DAC monitor mute was a red herring — it did not fix the pop.)

## Next steps

1. **Re-profile with the dongle attached locally** once the USB host hardware blocker is cleared
   (`rtl_tcp 127.0.0.1` or direct librtlsdr) — closes the USB-capture delta with a real number.
2. ~~Fix on-device audio routing via PipeWire~~ — **done 2026-06-30** (see Audio section).
3. Feed the measured load into the AppStore submission's hardware/CPU disclosure
   (`docs/distribution-gap-check.md` P2.8).
