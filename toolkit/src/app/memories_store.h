/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace toolkit {

// Frequency memories shared by the whole suite: the Memories app lists them,
// the SDR and the Scanner add to them. One JSON file in the shared config
// directory, ~/.config/zeroradio/memories.json:
//
//   {"version": 1, "memories": [
//     {"name": "Luqa Radar", "hz": 128150000, "mode": "AM",
//      "tags": ["luqa", "airband"], "note": "arrivals and departures"}
//   ]}
//
// `hz` is an integer in Hz. `mode` is one of the SDR mode names: WFM, NFM, AM,
// USB, LSB, CW. The first tag is the group the list shows the entry under.
// `note` is optional. A memory is identified by (hz, name).
struct Memory {
    std::string name;
    int64_t hz = 0;
    std::string mode;
    std::vector<std::string> tags;
    std::string note;
};

// The shared file. Empty if no config directory is known (see config_dir()).
std::filesystem::path memories_file();

// The saved memories, in file order. Empty when the file is missing or cannot
// be parsed: the apps never crash on a bad file.
std::vector<Memory> load_memories();

// Writes the whole list atomically: a per-process temporary file, flushed to
// storage, then renamed over the target. Returns false if nothing was written.
bool save_memories(const std::vector<Memory>& all);

// Appends one memory and saves. Returns false if the name is empty, hz is not
// positive, a memory with the same (hz, name) exists, or the save failed.
// add_memory() and remove_memory() hold a lock file (memories.json.lock) across
// the load and the save, so concurrent writers never lose an update.
bool add_memory(const Memory& m);

// Removes the memory with this (hz, name) and saves. Returns false if there is
// no such memory, or if the save failed.
bool remove_memory(int64_t hz, const std::string& name);

} // namespace toolkit
