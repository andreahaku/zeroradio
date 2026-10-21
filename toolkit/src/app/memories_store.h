/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
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

// The first memory on this frequency in this mode, whatever its name: lets the
// SDR and the Scanner say "already saved" instead of adding a second entry.
std::optional<Memory> find_memory(int64_t hz, const std::string& mode);

// The group a memory saved from the SDR or the Scanner goes in by default.
inline constexpr const char* kSavedGroup = "saved";

// The name proposed for a new memory: the frequency in MHz, to the kHz (or
// to the 10 Hz when it is not a whole kHz, as 118.00833), then the mode:
// "88.700 WFM".
std::string memory_label(int64_t hz, const std::string& mode);

// The channel line of the save dialog: "88.700 MHz  WFM".
std::string memory_info(int64_t hz, const std::string& mode);

// A mode for a frequency that comes without one (a Scanner peak): AM below
// 30 MHz (shortwave), WFM in the FM broadcast band (87.5-108 MHz), AM in the
// airband (108-137 MHz), NFM elsewhere. The Memories EDIT page changes it.
std::string guess_mode(int64_t hz);

// A channel about to be saved: the SDR's tuning or a Scanner peak. The save
// dialog adds the name and the group.
struct MemoryDraft {
    int64_t hz = 0;
    std::string mode;
};

// Saves the draft under `name`, in `group` (no tag when empty), and returns the
// message to show: "Saved: <name>", or why it was not saved.
std::string save_draft(const MemoryDraft& draft, const std::string& name,
                       const std::string& group);

// Replaces the memory with this (hz, name) by `replacement`, in the same place
// in the file. Returns false if there is no such memory, if the replacement
// is invalid (empty name, hz not positive), if it would take the identity of
// another memory, or if the save failed. Holds the same lock as add_memory().
bool update_memory(int64_t hz, const std::string& name, const Memory& replacement);

// Removes the memory with this (hz, name) and saves. Returns false if there is
// no such memory, or if the save failed.
bool remove_memory(int64_t hz, const std::string& name);

} // namespace toolkit
