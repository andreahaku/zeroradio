# Scanner

A wide-band sweep that shows what is transmitting across tens or hundreds of MHz. Find a peak, then open it in the SDR app to listen. It needs an RTL-SDR dongle in the USB-A port.

## Pages

Key 4 switches between two pages.

- 1 Waterfall: spectrum and scrolling waterfall over the swept range. Reference lines at 1/4, 1/2 and 3/4 of the span. A yellow dot marks each detected peak. The selected peak's dot is larger.
- 2 Peaks: a table of FREQ (MHz), POWER and AGE, with a cursor band on the selected row.

## Keys

- Waterfall: 5 zoom out (wider), 6 tune dialog, 7 zoom in (narrower), 8 dark/light theme.
- Peaks: 5 cursor up, 6 cursor down, 7 open in SDR, 8 next sort order.

Tune dialog (SWEEP, key 6): the fields show the current values, and typing replaces them. Type the centre in MHz, Enter, then the total span in MHz, Enter. F/X move between the two fields. The sweep covers centre +/- span/2. Digits, dot (SYM + .), up to 3 decimals, and Backspace edit. Esc cancels.

Open in SDR (Peaks, key 7 or Enter): quits the Scanner and starts the SDR app tuned to the selected peak.

Sort (Peaks, key 8): power, frequency, age, each descending then ascending. The header shows the full name (e.g. PWR v), the key a short code.

## Global keys

- 4: the next page of keys. The key shows the page name: SWEEP, PEAKS. Hold 4 for the page before.
- F/X (up/down arrows): zoom in/out on the Waterfall page, move the cursor on the Peaks page.
- Enter: on the Peaks page, open the selected peak in the SDR.
- Z/C (left/right arrows): move the swept band down/up by a quarter of its span on the Waterfall page.
- TAB: switch to the SDR app.
- M: save the selected peak (the cursor in the Peaks list, the strongest by default) as a memory, rounded to the kHz. A dialog proposes a name and the group saved: type to replace them, Enter moves from the name to the group and then saves, Esc cancels. Holding 6 on the Waterfall page does the same. The mode is a guess: WFM in the FM broadcast band, AM in the airband, NFM elsewhere.
- SYM + 4 to 8: a tooltip that says what that key does on this page, in your language (English, Italian, French, German, Spanish, Japanese or Chinese).
- H or Fn+H: this help. F/X scroll it, Esc closes it.
- Esc: quit to the hub. Hold Esc 3 s to leave ZeroRadio for the launcher. Let go before 3 s to cancel.

## Tips

- A narrow span sweeps faster and resolves close signals better.
- The swept range is saved for the next launch.
- The dongle drains the battery fast. Quit when you are done.
