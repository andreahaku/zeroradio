# Changelog

All notable changes to ZeroRadio. Versions follow semantic versioning.

## 1.0.0

First public release: a suite of radio tools for the [M5Stack CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero), driven by an RTL-SDR dongle in the USB-A port. Every decoder runs on the device, and the package installs everything it needs.

- SDR: live spectrum and waterfall, WFM, FM, AM, USB, LSB and CW audio on the speaker, frequency entry, zoom, band presets.
- Scanner: wide-band sweep with rtl_power, peaks list with sort; a peak opens in the SDR app.
- ISM: 433/868 MHz device sniffer with rtl_433 (sensors, TPMS, remotes).
- ADS-B: aircraft list, radar and world map with the bundled readsb decoder.
- AIS: ship list, radar and world map with the bundled AIS-catcher decoder.
- Location: set your position once for the whole suite, by city name (offline list of 34,000 cities), coordinates or the GPS of the Cap LoRa-1262-GPS.
- Detailed worldwide map (Natural Earth 10m) for the ADS-B and AIS map views.
- TAB switches between SDR and Scanner; F/X scroll every list; H opens the help of each app.
- Battery level in every app, with a charge indicator.
