# Scanner

A wide-band sweep that shows what is transmitting across tens or hundreds of MHz. Find a peak, then open it in the SDR app to listen. It needs an RTL-SDR dongle in the USB-A port.

## Pages

Key 4 switches between two pages.

- 1 Waterfall: spectrum and scrolling waterfall over the swept range. Reference lines at 1/4, 1/2 and 3/4 of the span. A yellow dot marks each detected peak. The selected peak's dot is larger.
- 2 Peaks: a table of FREQ (MHz), POWER and AGE, with a cursor band on the selected row.

## Keys

- Waterfall: 5 zoom out (wider), 6 tune dialog, 7 zoom in (narrower), 8 dark/light theme.
- Peaks: 5 cursor up, 6 cursor down, 7 open in SDR, 8 next sort order.

Tune dialog (Waterfall, key 6): type the centre in MHz, Enter, then the total span in MHz, Enter. The sweep covers centre +/- span/2. Digits, dot (up to 3 decimals) and Backspace edit. Esc cancels.

Open in SDR (Peaks, key 7): quits the Scanner and starts the SDR app tuned to the selected peak.

Sort (Peaks, key 8): power, frequency, age, each descending then ascending. The header shows the full name (e.g. PWR v), the key a short code.

## Global keys

- F/X (up/down arrows): move the cursor on the Peaks page.
- TAB: switch to the SDR app.
- H: this help. F/X scroll it, Esc closes it.
- Esc: quit to the hub.

## Tips

- A narrow span sweeps faster and resolves close signals better.
- The swept range is saved for the next launch.
- The dongle drains the battery fast. Quit when you are done.
