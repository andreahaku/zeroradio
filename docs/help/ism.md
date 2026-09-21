# ISM

Lists nearby ISM-band devices decoded by rtl_433: weather sensors, tyre-pressure monitors (TPMS), remotes, energy meters. It needs an RTL-SDR dongle in the USB-A port and listens on 433.92 MHz.

## Pages

Key 4 cycles three pages.

- 1 List: MODEL, TYPE and AGE of each device. The cursor band marks the current row, a dot marks the locked device. The title shows the locked device.
- 2 Detail: every reported field of the locked device (or the cursor device when none is locked): ID, channel, temperature, humidity, pressure, battery, RSSI, SNR, frequency, last heard, plus extra fields.
- 3 Settings: Theme and TTL.

## Keys

- List: 5 next sort (MODEL, AGE, RSSI), 6 previous device, 7 next device, 8 lock/unlock the cursor device.
- Detail: 5 previous device, 6 next device, 8 lock/unlock. Key 7 does nothing.
- Settings: 5 previous row, 6 next row, 7 change the value, 8 quit.

Theme switches dark/light. TTL is how long a silent device stays listed: 30, 60, 120 or 300 s.

## Global keys

- F/X (up/down arrows): move the cursor on the List, Detail and Settings pages.
- H: this help. F/X scroll it, Esc closes it.
- Esc: quit to the hub.

## Tips

- Many sensors transmit only every 30-60 s. Wait a minute before you decide nothing is there.
- TPMS sensors send mostly while the car moves.
- The dongle drains the battery fast. Quit when you are done.
