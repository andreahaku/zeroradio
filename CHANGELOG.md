# Changelog

All notable changes to ZeroRadio. Versions follow semantic versioning.

## Unreleased

- Keys: left/right (Z/C) repeat while held, after 400 ms and then ten times a second, in every app. In the SDR the tune keys 5 and 7 on page 1 repeat too.
- Scanner: peaks and the waterfall sat half a bin above the real frequency with rtl_power (about +30 kHz on a 20 MHz span). rtl_power gives FFT bin centres, not intervals, so the half-bin offset now applies to hackrf_sweep only.
- Scanner: Enter on the Peaks page opens the selected peak in the SDR, like key 7.
- SDR: the audio no longer drops out every few seconds. The capture rate is now always a multiple of 240 kHz, so the audio comes out at exactly 48 kHz. Before, the device's 1.024 Msps (and 1 or 1.5 MHz zoom on the desktop) gave up to 51.2 kHz, faster than the speaker plays it.
- SDR: the device captures up to 2.16 Msps instead of 1.024 Msps, so the 1, 1.5 and 2 MHz zooms show their full width. Before, they stretched about 1 MHz under the wider labels, so stations sat under the wrong frequency. A zoom wider than `SDR_SAMPLE_RATE` now keeps the right scale and leaves the edges blank.
- SDR: calibration moves from its own page to key 8 of the GAIN page (CAL), in place of exit, which Esc already does. It zooms in as far as it goes, shows the correction in ppm and in Hz over the spectrum, and OK puts the zoom back.

## 1.2.0

- Memories: a new app lists your saved frequencies, grouped by tag, and opens one in the SDR with its mode. The list lives in `~/.config/zeroradio/memories.json`, shared by the whole suite. The repository has an example list with the airband, marine VHF and NAVTEX frequencies around Malta, and the app README explains how to copy it to the device.
- Memory scan: mark memories (or pick a group) in the Memories app and scan them in the SDR. The scan stops on a channel where the squelch opens, listens until the transmission ends, then moves on. Keys pause, skip, lock out a busy channel or stop. Channels close together are checked from one frame without retuning.
- SDR: squelch, on a new SQL page. It keeps the speaker quiet until a signal reaches the level you set, and holds the audio 2.5 s after the signal drops so pauses in speech pass. A tick on the S-meter shows the level.
- SDR: the S-meter shows the signal inside the passband instead of the strongest signal on screen.
- SDR: frequency correction for the dongle's crystal, on a new CAL page. Keys 5 and 7 step it by 0.1 ppm while you watch a known station, and key 6 takes a value in ppm, such as the one `rtl_test -p` reports. It is saved and applies to every frequency.
- SDR: the `SDR_MODE` environment variable (WFM, NFM, AM, USB, LSB or CW) sets the mode at start, next to `SDR_FREQ`.

## 1.1.0

- Arrow keys work on every page: F/X (up/down) and Z/C (left/right). SDR: left/right tune by one step, up/down zoom. Scanner: up/down zoom and left/right move the swept band. ADS-B and AIS: up/down zoom the radar and map, left/right step through the contacts. ISM: left/right step through the devices. Settings: left/right change the value both ways.
- Key 4 shows the name of the tool page (TUNE, ZOOM, LIST, RADAR, SETUP…) on a white tab, as in ZeroTracker, instead of its number.
- Hold key 4 to go back one tool page, as in ZeroTracker. A tap still shows the next page.
- SYM + 4 to 8 shows what that key does on the current page. The tooltip follows the system language: English, Italian, French, German, Spanish, Japanese or Chinese.
- Hold Esc 3 s to leave ZeroRadio for the launcher, as in ZeroTracker. The same toast as ZeroTracker, "Hold ESC 3 s to exit", stays up while you hold, and letting go cancels it. A short press still goes back to the menu.
- The menu comes back on the app you used last, also after a TAB switch between SDR and Scanner.
- Frequency entry: the "." key (SYM + .) now types the decimal point, so you can enter values such as 98.525 MHz in the SDR and the Scanner.
- ADS-B and AIS: two closer zoom steps, 2 NM and 1 NM. On the map view the arrows move the centre of the map, and Enter centres it on your position again. Hold key 8 on the map to follow the selected aircraft or ship: an orange dot shows left of its name in the title.
- Scanner: the tune dialog accepts typing again. Its fields started full with the current value, so every digit was dropped; now the first digit replaces the value, and F/X move between centre and span.
- SDR: Fn+A mutes, Fn+S and Fn+D change the volume from any page, with a short on-screen readout.
- SDR: the narrow FM mode is now labelled NFM, next to WFM.
- The battery level is green, yellow, orange or red as the charge drops.
- The menu footer reads ENTER Open and ESC Exit.

## 1.0.0

First public release: a suite of radio tools for the [M5Stack CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero), driven by an RTL-SDR dongle in the USB-A port. Every decoder runs on the device, and the package installs everything it needs.

- SDR: live spectrum and waterfall, WFM, FM, AM, USB, LSB and CW audio on the speaker, frequency entry, zoom, band presets.
- Scanner: wide-band sweep with rtl_power, peaks list with sort; a peak opens in the SDR app.
- ISM: 433/868 MHz device sniffer with rtl_433 (sensors, TPMS, remotes).
- ADS-B: aircraft list, radar and world map with the bundled readsb decoder.
- AIS: ship list, radar and world map with the bundled AIS-catcher decoder.
- Location: set your position once for the whole suite, by city name (offline list of 34,000 cities), coordinates or the GPS of the Cap LoRa-1262-GPS.
- Detailed worldwide map (Natural Earth 10m) for the ADS-B and AIS map views.
- TAB switches between SDR and Scanner; F/X scroll every list; H (or Fn+H) opens the help of each app; holding Esc returns to the launcher.
- Battery level in every app, with a charge indicator.
