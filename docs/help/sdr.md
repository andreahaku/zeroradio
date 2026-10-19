# SDR

A live receiver: spectrum, waterfall and demodulated audio (WFM, NFM, AM, USB, LSB, CW). It needs an RTL-SDR dongle in the USB-A port and an antenna for the band you listen to.

## Pages

Key 4 cycles seven tool pages. The screen stays the same: mode on the left, tuned frequency in the middle, S-meter on the right, then spectrum and waterfall. The S-meter shows the signal inside the green passband. The green band is the demodulated passband.

- 1 Tuning: tune the frequency.
- 2 Zoom: span, band preset, mode.
- 3 Visual: theme and display options.
- 4 Audio: mute and volume.
- 5 Squelch: silence the noise between transmissions.
- 6 Settings: RF gain and exit.
- 7 Calibration: frequency correction for the dongle.

## Keys

- Page 1: 5 tune down, 6 frequency entry, 7 tune up, 8 coarse/fine step (the key shows the step).
- Page 2: 5 zoom out, 6 next band (FM, Air, 2m, 70cm), 7 zoom in, 8 next mode.
- Page 3: 5 dark/light theme, 6 frequency grid, 7 waterfall/spectrum split, 8 peak hold.
- Page 4: 5 mute, 6 volume down, 7 volume up, 8 shows the volume.
- Page 5: 5 squelch level down, 6 squelch on/off, 7 squelch level up, 8 shows the level.
- Page 6: 5 gain down, 6 gain up, 7 auto gain on/off, 8 exit.
- Page 7: 5 correction down 0.1 ppm, 6 correction entry, 7 correction up 0.1 ppm, 8 shows the correction in ppm.

Frequency entry (page 1, key 6): type MHz with digits and a dot (SYM + .), up to 4 decimals, for example 98.525, Backspace deletes, Enter tunes, Esc cancels.

Correction entry (page 7, key 6): type ppm with up to one decimal, for example 2.4. The - key changes the sign. The dialog shows how many Hz the value moves the signals on screen at the current frequency. A positive value moves them to the right. Enter saves, Esc cancels.

## Squelch

The squelch keeps the speaker quiet until a signal comes in, so narrow FM and AM do not hiss between transmissions. On page 5, key 6 turns it on or off and keys 5 and 7 move its level by 5. Keys 5 and 7 also turn it on, like the squelch knob of a radio. A white tick on the S-meter marks the level. The S-meter is grey while the squelch holds the audio back and green while it plays.

The audio opens when the S-meter reaches the tick. It closes when the signal drops 8 points below the tick for 2.5 s, so a short pause in a conversation does not cut it. To set it, tune to a quiet channel, then raise the level until the noise stops.

## Memory scan

The Memories app starts a scan of the channels you pick (SCAN page there). The SDR then shows a SCAN page with these keys: 5 pause or resume, 6 skip to the next channel, 7 leave this channel out until the scan ends, 8 stop the scan and stay on this channel. A label at the top left says what the scan is doing: the group it is checking ("SCAN 2/5"), or the channel it stopped on.

The scan stops on a channel when the S-meter there reaches the squelch level, and listens while the squelch is open. When the transmission ends and the squelch closes, after its 2.5 s hold, the scan moves on: first to the other busy channels nearby, then to the next group. Skip does the same. The scan turns the squelch on if it was off, so set its level first on a quiet channel. Turning the squelch off ends the scan. Channels close together, such as the marine channels, are checked all at once from one screen of spectrum, so a quiet band takes a quarter of a second. Tuning by hand, a band preset or a typed frequency ends the scan. The SDR saves the channel the scan ended on, not the channels it passed.

## Calibrate the frequency

The crystal of every dongle is a little off, so a station shows a little away from its frequency. To correct it, tune to a station whose frequency you know, such as an airport ATIS in AM. Zoom in until you see its carrier. On page 7, if the carrier sits left of the centre line, raise the correction with key 7. If it sits right, lower it with key 5. Stop when it sits on the line. The value is saved and applies to every frequency. If you know your dongle's ppm, for example from `rtl_test -p`, type it with key 6.

## Global keys

- 4: the next page of keys. The key shows the page name: TUNE, ZOOM, VIEW, AUDIO, SQL, GAIN, CAL, and SCAN during a memory scan. Hold 4 for the page before.
- Arrows (F/X/Z/C): Z/C tune down/up by one step, F/X zoom in/out.
- Fn+A mutes or unmutes, Fn+S and Fn+D turn the volume down and up, from any page.
- TAB: switch to the Scanner app.
- SYM + 4 to 8: a tooltip that says what that key does on this page, in your language (English, Italian, French, German, Spanish, Japanese or Chinese).
- H or Fn+H: this help. F/X scroll it, Esc closes it.
- Esc: quit to the hub. Hold Esc 3 s to leave ZeroRadio for the launcher. Let go before 3 s to cancel.

## Tips

- The step depends on the mode: WFM 100k/10k, NFM 25k/5k, AM 9k/1k, SSB 1k/100, CW 500/100 Hz.
- A narrower zoom lowers the sample rate and the CPU load.
- The dongle drains the battery fast. Quit when you stop listening.
- Frequency, mode, zoom, volume, squelch, gain, frequency correction and display options are saved for the next launch.
