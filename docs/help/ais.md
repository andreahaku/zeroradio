# AIS

Shows ships near you from their AIS broadcasts on 161.975 and 162.025 MHz: a list, a radar or map, and a detail view. It needs an RTL-SDR dongle in the USB-A port and an antenna for 162 MHz with a view of the sea. The radar centre comes from Settings > Location.

## Pages

Key 4 cycles four pages.

- 1 List: MMSI (or name), SOG, COG, HDG, DST. Colours show the navigation status. A dot marks the locked vessel.
- 2 Radar: north-up scope with range rings, course markers and trails, or a Mercator map.
- 3 Detail: fields of the locked vessel (MMSI, speed, course, heading, range, bearing, ship type, destination, status), with a mini-radar.
- 4 Settings: a list of options.

## Keys

- List: 5 next sort (MMSI, DST, SOG, COG), 6 previous vessel, 7 next vessel, 8 lock/unlock the cursor vessel.
- Radar: 5 zoom in, 6 zoom out (up to AUTO), 7 trails on/off, 8 radar/map.
- Detail: 5 zoom in, 6 zoom out, 7 trails on/off, 8 show other traffic on/off.

Zoom steps: 5, 10, 20, 50, 100 and 200 NM, then AUTO.
- Settings: 5 previous row, 6 next row, 7 change the value, 8 quit.

Settings rows: Theme, Units (NM/km), TTL, Range, Trails, Map view, Location.

Location (Settings, key 7 on the Location row): type a city or "lat, lon". The first row is the GPS. Fn+F/X (arrows) pick a row, Enter applies, Esc cancels. ADS-B and AIS share this position.

## Global keys

- F/X (up/down arrows): move the cursor on the List and Settings pages.
- H: this help. F/X scroll it, Esc closes it.
- Esc: quit to the hub.

## Tips

- AIS works only near the coast. Inland you will see no ships.
- Ships report less often than aircraft. Give the list a few minutes.
- Set the Location first, or range and bearing are wrong.
- The dongle drains the battery fast. Quit when you are done.
