# ADS-B

Shows aircraft near you from their 1090 MHz ADS-B broadcasts: a list, a radar or map, and a detail view. It needs an RTL-SDR dongle in the USB-A port and an antenna for 1090 MHz. The radar centre comes from Settings > Location.

## Pages

Key 4 cycles four pages.

- 1 List: CALL, ALT, SPD, TRK, DST. Colours show the category, red is an emergency squawk. A dot marks the locked aircraft.
- 2 Radar: north-up scope with range rings, heading arrows and trails, or a Mercator map.
- 3 Detail: fields and status of the locked aircraft, with a mini-radar.
- 4 Settings: a list of options.

## Keys

- List: 5 next sort (CALL, DST, SPD, ALT, TRK), 6 previous aircraft, 7 next aircraft, 8 lock/unlock the cursor aircraft.
- Radar: 5 zoom in, 6 zoom out (up to AUTO), 7 trails on/off, 8 radar/map.
- Detail: 5 zoom in, 6 zoom out, 7 trails on/off, 8 show other traffic on/off.

Zoom steps: 1, 2, 5, 10, 20, 50, 100 and 200 NM, then AUTO. Below 5 NM the coastline is coarse: the map data is Natural Earth 1:10m.
- Settings: 5 previous row, 6 next row, 7 change the value, 8 quit.

Settings rows: Theme, Units (NM/km), TTL, Range, Trails, Ground, Emerg only, Map view, Location.

Location (Settings, key 7 on the Location row): type a city or "lat, lon". The first row is the GPS. Fn+F/X (arrows) pick a row, Enter applies, Esc cancels. ADS-B and AIS share this position.

## Global keys

- 4: the next page of keys. The key shows the page name: LIST, RADAR (MAP), DETAIL, SETUP. Hold 4 for the page before.
- F/X (up/down arrows): move the cursor on the List and Settings pages, zoom in/out on the Radar and Detail pages.
- Z/C (left/right arrows): previous/next aircraft on the Radar and Detail pages, change the value in Settings.
- Map view (Radar page): the arrows move the map centre by a quarter of the range. Enter centres it on your position again.
- SYM + 4 to 8: a tooltip that says what that key does on this page, in your language (English, Italian, French, German, Spanish, Japanese or Chinese).
- H or Fn+H: this help. F/X scroll it, Esc closes it.
- Esc: quit to the hub. Hold Esc 3 s to leave ZeroRadio for the launcher. Let go before 3 s to cancel.

## Tips

- Set the Location first, or range and bearing are wrong.
- 1090 MHz needs a short antenna held high, with a clear view of the sky.
- AUTO range fits the rings to the farthest aircraft.
- The dongle drains the battery fast. Quit when you are done.
