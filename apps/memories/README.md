# Memories

Memories keeps a personal list of frequencies and opens a selected entry in SDR with its saved modulation mode. It does not use the radio dongle while the list is open.

## List

The list shows name, frequency, and mode. Entries are grouped by their first tag. Within each group, press **TAB** to switch between name and frequency order. The status line shows the current order and any active search.

The sample list includes airband, marine, AIS, and NAVTEX frequencies around Malta and Gozo. Frequencies below 1 MHz are shown in kHz; higher frequencies are shown in MHz.

Press **4** to switch between the MEM and SCAN key pages. On SCAN, press **5** to mark or unmark a memory. A `*` beside its name shows that it is marked. Marks stay in place when you sort or search, including for entries hidden by the current search.

Press **6** to scan the marked memories, or every memory in the selected entry's group when nothing is marked. Press **7** to scan the selected entry's group even when other memories are marked. Press **8** to clear all marks. A confirmation screen shows how many memories will be scanned; press **Enter** to start in SDR or **Esc** to cancel. Channels are sent in frequency order. Up to 64 can be scanned at once: marking a 65th entry is refused, and a larger group is limited to its first 64 frequencies with the limit shown before starting.

## Keys

| Key | Action |
| --- | --- |
| 5 / F | Move up, skipping group headings |
| 6 / X | Move down, skipping group headings |
| 7 / Enter | Open the selected memory in SDR |
| 8 | Search names; Enter applies the filter, Esc cancels, an empty query shows all |
| 4 | Switch between MEM and SCAN key pages |
| 5 on SCAN | Mark or unmark the selected memory |
| 6 on SCAN | Scan marked memories, or the selected group if none are marked |
| 7 on SCAN | Scan the selected group, ignoring marks |
| 8 on SCAN | Clear all marks |
| TAB | Switch between name and frequency order |
| H | Open in-app help |
| Esc | Return to the ZeroRadio hub |

SYM + 4 through 8 shows key hints. Hold Esc for three seconds to return to the device launcher.

## Example list

The app reads `~/.config/zeroradio/memories.json`, a file shared by the whole suite. It starts empty. [`assets/memories.example.json`](assets/memories.example.json) is a ready list for the island of Gozo, Malta: 23 frequencies in four groups.

| Group (first tag) | Entries | Mode |
| --- | --- | --- |
| `luqa` | Luqa airport (LMML): radar, tower, ground, apron, both ATIS, and the 121.500 MHz air emergency guard | AM |
| `marine` | Marine VHF channels 06, 08, 10 to 14, 16, 70, 72 and 77 | NFM |
| `ais` | AIS A and B. Use the AIS app to decode them | NFM |
| `navtex` | NAVTEX at 490 and 518 kHz. Needs a dongle with direct sampling, such as the RTL-SDR Blog V4 | USB |

Copy it to the device from a computer:

```bash
scp apps/memories/assets/memories.example.json <user>@<cardputer>:.config/zeroradio/memories.json
```

The Luqa frequencies come from third-party sources, not the official AIP, so some can differ by a few kHz. The marine channels are the standard ITU channels.

### Make your own list

On the device, press M in the SDR to save the tuned frequency and mode, or in the Scanner to save the selected peak. A dialog asks for the name and the group (the first tag). Both apps save through `toolkit::save_draft()`, so they never write the file by hand.

On a computer, copy the example and edit it with any text editor. Each entry has these fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `name` | yes | The name in the list. Keep it short: the column fits about 20 characters |
| `hz` | yes | The frequency in Hz, as a whole number: `128150000` for 128.150 MHz |
| `mode` | no | The SDR mode: `WFM`, `NFM`, `AM`, `USB`, `LSB` or `CW`. Without it, the SDR keeps its last mode |
| `tags` | no | A list of words. The first one is the group in the list. Without tags, the entry goes under `other` |
| `note` | no | Free text for yourself. The app does not show it yet |

```json
{"version": 1, "memories": [
  {"name": "Tower", "hz": 118700000, "mode": "AM", "tags": ["airport"], "note": "local tower"}
]}
```

Use AM for the airband (118 to 137 MHz) and NFM for marine VHF, amateur repeaters and PMR446. An entry without a name or with a frequency that is not a positive whole number is skipped. A file that is not valid JSON shows an empty list, so check it with a JSON validator if your list does not appear.
