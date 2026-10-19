#!/bin/sh
# Launch wrapper for the Radio hub on CardputerZero.
# Sets device-appropriate SDR runtime env, then execs the hub binary; the child
# apps the hub spawns (sdr_app, adsb_app, ...) inherit this environment.
#
# Installed by the .deb next to the hub binary; the Radio .desktop Exec points here.
cd "$(dirname "$0")" || exit 1

# Sample-rate cap: enough for the widest (2 MHz) zoom. Measured on the device
# with that zoom and WFM: sdr_app ~1.65 cores of 4 and no lost IQ at 2.16 Msps,
# against ~1.37 at 960 ksps. This is a cap: the rate follows the zoom below it.
export SDR_SAMPLE_RATE="${SDR_SAMPLE_RATE:-2160000}"

# Audio sink. The device image is PipeWire-managed (the ES8388 codec is held
# exclusively by PipeWire), so the SDR ALSA output is routed through the `pipewire`
# PCM. This requires the `pipewire-alsa` bridge package (declare it as a .deb
# Depends). Override with SDR_ALSA_DEV= on systems without PipeWire.
export SDR_ALSA_DEV="${SDR_ALSA_DEV:-pipewire}"

# Decoders. With the dongle in the USB-A port every app starts its own decoder
# (rtl_tcp, readsb, AIS-catcher, rtl_433, rtl_power) and stops it on exit. To use
# a dongle on another machine instead, set e.g. SDR_RTLTCP=<host>:1234.

exec ./radio_app
