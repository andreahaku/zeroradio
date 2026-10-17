# SDR

A live receiver: spectrum, waterfall and demodulated audio (WFM, NFM, AM, USB, LSB, CW). It needs an RTL-SDR dongle in the USB-A port and an antenna for the band you listen to.

## Pages

Key 4 cycles five tool pages. The screen stays the same: mode on the left, tuned frequency in the middle, S-meter on the right, then spectrum and waterfall. The green band is the demodulated passband.

- 1 Tuning: tune the frequency.
- 2 Zoom: span, band preset, mode.
- 3 Visual: theme and display options.
- 4 Audio: mute and volume.
- 5 Settings: RF gain and exit.

## Keys

- Page 1: 5 tune down, 6 frequency entry, 7 tune up, 8 coarse/fine step (the key shows the step).
- Page 2: 5 zoom out, 6 next band (FM, Air, 2m, 70cm), 7 zoom in, 8 next mode.
- Page 3: 5 dark/light theme, 6 frequency grid, 7 waterfall/spectrum split, 8 peak hold.
- Page 4: 5 mute, 6 volume down, 7 volume up, 8 shows the volume.
- Page 5: 5 gain down, 6 gain up, 7 auto gain on/off, 8 exit.

Frequency entry (page 1, key 6): type MHz with digits and a dot (SYM + .), up to 4 decimals, for example 98.525, Backspace deletes, Enter tunes, Esc cancels.

## Global keys

- 4: the next page of keys. The key shows the page name: TUNE, ZOOM, VIEW, AUDIO, GAIN. Hold 4 for the page before.
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
- Frequency, mode, zoom, volume, gain and display options are saved for the next launch.
