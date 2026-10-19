# Memories

Browse saved frequencies and open one in SDR. The list is grouped by the first tag of each memory. The selected row is highlighted. Group headings are skipped by the cursor.

## Keys

- **F/X or MEM 5/6**: move up or down.
- **Enter or MEM 7**: open the selected frequency in SDR with its saved mode.
- **MEM 8**: search memory names. Type a query and press Enter. Clear the query and press Enter to show everything. Esc cancels the dialog.
- **4**: switch between the MEM and SCAN key pages.
- **SCAN 5**: mark or unmark the selected memory. A `*` beside its name shows a mark. Marks remain after sorting or searching.
- **SCAN 6**: scan marked memories, or every memory in the current group if none are marked.
- **SCAN 7**: scan the current group even if other memories are marked.
- **SCAN 8**: clear all marks.
- **TAB**: switch sorting within each group between name and frequency.
- **H**: open this help. Esc closes it.
- **Esc**: return to the hub. Hold for three seconds to leave ZeroRadio.

The status line shows the number of matches, sort order, and search query. If the list is empty, load a `memories.json` file in the shared ZeroRadio config directory. See the Memories README for an example.

Before a scan starts, the app shows the number of channels. Press Enter to open the scanner in SDR or Esc to cancel. Channels run in frequency order. Each scan accepts up to 64 memories; a larger group is limited to its first 64, and the confirmation screen shows the original count.
