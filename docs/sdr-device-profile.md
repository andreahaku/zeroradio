# SDR on-device profile — CardputerZero (CM0) @ 1.024 Msps

CPU, RAM and thermal profile of `sdr_app` on the [CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero), measured on 2026-06-30.

## Setup

- **Device**: Raspberry Pi Compute Module 0, 4× Cortex-A (pinned 1.0 GHz), 415 MB RAM,
  kernel `6.12.75+rpt-rpi-v8`, Debian trixie. APPLaunch running in the background.
- **Capture**: the dongle (RTL-SDR Blog V4) sat on a separate host that ran
  `rtl_tcp -a 0.0.0.0 -p 1234`, and the device connected to it over the network. The device paid
  for the network receive, the DSP and the display, but not for the USB capture.
- **Run environment**: `SDR_RTLTCP=<host>:1234`, `SDR_SAMPLE_RATE=1024000`, rendering to `/dev/fb0`.
- **Audio**: off. The profile covers DSP and display only.
- **Profiler**: `scripts/sdr-profile.sh 30 sdr_app` (per-core load from `/proc/stat`, since the
  device has no `mpstat`).

In the released suite the dongle plugs into the device's USB-A port, and `sdr_app` starts its own
`rtl_tcp` on `127.0.0.1`. **A re-profile with the local dongle is pending.**

## Results

| Metric | Value | Verdict |
|--------|-------|---------|
| `sdr_app` CPU | 90–150 %, median **~118 %** → **~1.2 of 4 cores** | ~30 % of quad-core capacity |
| Per-core busy (system, under load) | cpu0 47 % · cpu1 40 % · cpu2 19 % · cpu3 42 % (~37 % avg) | multi-threaded DSP, spread; ~2.5 cores idle |
| RAM (RSS) | **~9.6 MB**; MemAvailable 173–201 MB steady | the 415 MB RAM is no constraint for SDR |
| Temperature | **59–60 °C** steady | within limits |
| Throttling | `get_throttled = 0x0` throughout; ARM clock pinned at 1.0 GHz | no thermal or voltage cap |
| Swap | 99 MB used (present before the run, no pressure during it) | unrelated to SDR |

At 1.024 Msps with waterfall and demodulation, the SDR app used **~1.2 cores, <10 MB RAM, 60 °C and
no throttling**, with ~2.5 cores left idle.

## Expected cost of the local dongle

With the dongle on the device, the device also runs `rtl_tcp` and pays for libusb/librtlsdr
draining IQ at 1.024 Msps. On a Pi that is typically **~0.2–0.4 of a core**, which puts the
estimated total at **~1.4–1.6 cores**. This is an estimate, not a measurement. The re-profile will
replace it: run `scripts/sdr-profile.sh` twice, once for `sdr_app` and once for `rtl_tcp`, while the
app streams from the local dongle.

If the load ever competes with the UI, CPU affinity (`taskset`) can dedicate cores to the SDR
pipeline.

## Audio routing

The device image runs **PipeWire**, which holds the ES8388 codec exclusively. Direct ALSA output
fails on every target: `plughw:1,0` returns *Device busy*, `default` returns *ENOTSUPP (524)*, and
`dmix`/`hw` hit the same exclusive hold.

The SDR audio therefore goes through the `pipewire-alsa` bridge:

1. The `zeroradio` package depends on `pipewire-alsa`, which exposes the `pipewire` ALSA PCM.
2. `radio-launch.sh` exports `SDR_ALSA_DEV=pipewire`. The hardware mixer still targets `hw:1` through
   `SDR_ALSA_CARD`, and `controlC1` is shareable with WirePlumber.

`wpctl status` showed `sdr_app` as a PipeWire client with `output_FL/FR →
bcm2835-i2s-ES8389 HiFi playback [active]`, and the audio played on the device speaker. APPLaunch
starts apps as user `pi` with `XDG_RUNTIME_DIR=/run/user/1000` set, so the PipeWire ALSA plugin
finds the session daemon without extra environment.

### Speaker noise at boot (older images)

On earlier CardputerZero images, the `99-pipewire-default.conf` file that `pipewire-alsa` installs made PipeWire the ALSA `default` device, and a few seconds of speaker noise followed every boot. The 2026-09-20 image with ZeroRadio 1.0.0 installed shows no noise. If it returns, disable that file (`mv .../99-pipewire-default.conf{,.disabled}`): SDR audio keeps working through the explicit `pipewire` PCM from `50-pipewire.conf`.

## Next steps

1. Re-profile with the dongle in the device's USB-A port.
