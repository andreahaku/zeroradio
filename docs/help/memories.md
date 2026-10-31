# Memories

Browse saved frequencies and open one in SDR. The list is grouped by the first tag of each memory. The selected row is highlighted. Group headings are skipped by the cursor.

## Keys

- **F/X or MEM 5/6**: move up or down.
- **Enter or MEM 7**: open the selected frequency in SDR with its saved mode.
- **MEM 8**: search memory names. Type a query and press Enter. Clear the query and press Enter to show everything. Esc cancels the dialog.
- **4**: switch between the MEM, SCAN and EDIT key pages.
- **SCAN 5**: mark or unmark the selected memory. A `*` beside its name shows a mark. Marks remain after sorting or searching.
- **SCAN 6**: scan marked memories, or every memory in the current group if none are marked.
- **SCAN 7**: scan the current group even if other memories are marked.
- **SCAN 8**: clear all marks.
- **EDIT 5**: rename the selected memory or change its group. Type to replace a field, Enter moves from the name to the group and then saves, Esc cancels.
- **EDIT 6**: switch the selected memory to the next mode: WFM, NFM, AM, USB, LSB, CW. It is saved at once.
- **EDIT 7**: delete the selected memory. The key shows DEL?. Press it a second time within 4 seconds to confirm. DEL? goes away by itself after 4 seconds, and when you move the cursor or change the page.
- **TAB**: switch sorting within each group between name and frequency.
- **H**: open this help. Esc closes it.
- **Esc**: return to the hub. Hold for three seconds to leave ZeroRadio.

The status line shows the number of matches, sort order, and search query.

## Adding memories

Press M in the SDR to save the frequency and mode you are tuned to, or in the Scanner to save the selected peak. A dialog proposes a name and the group saved. Type to replace them, Enter moves from the name to the group and then saves. You can also copy a whole `memories.json` file into the shared ZeroRadio config directory: see the Memories README for the format and an example list.

Before a scan starts, the app shows the number of channels. Press Enter to open the scanner in SDR or Esc to cancel. Channels run in frequency order. Each scan accepts up to 64 memories; a larger group is limited to its first 64, and the confirmation screen shows the original count.
