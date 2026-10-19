/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// toolkit::memories_store: the save/load round trip of the shared frequency
// memories, duplicates, removal, and tolerance to a missing or bad file.

#include "memories_store.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace toolkit;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

int main() {
    // Point the config directory at a fresh temporary directory.
    char tmpl[] = "/tmp/memories_store_test_XXXXXX";
    const char* dir = mkdtemp(tmpl);
    check(dir != nullptr, "temporary directory");
    if (!dir) return 1;
    setenv("XDG_CONFIG_HOME", dir, 1);

    check(memories_file() == std::filesystem::path(dir) / "zeroradio" / "memories.json",
          "file lives in the shared zeroradio directory");
    check(load_memories().empty(), "missing file -> empty list");

    Memory radar{"Luqa Radar", 128150000, "AM", {"luqa", "airband"}, "arrivals"};
    Memory port{"Mgarr Port Control", 156500000, "NFM", {"marittimo", "mgarr"}, ""};
    check(add_memory(radar), "add first");
    check(add_memory(port), "add second");
    check(!add_memory(radar), "duplicate (hz, name) refused");
    check(!std::filesystem::exists(memories_file().string() + ".tmp"), "no temporary file left");

    auto all = load_memories();
    check(all.size() == 2, "two memories saved");
    if (all.size() == 2) {
        check(all[0].name == "Luqa Radar" && all[0].hz == 128150000 && all[0].mode == "AM",
              "first memory fields");
        check(all[0].tags.size() == 2 && all[0].tags[0] == "luqa" && all[0].tags[1] == "airband",
              "tags kept in order");
        check(all[0].note == "arrivals", "note kept");
        check(all[1].name == "Mgarr Port Control" && all[1].note.empty(), "second memory, empty note");
    }

    // Same name on another frequency is a different memory.
    Memory radar2 = radar;
    radar2.hz = 118350000;
    check(add_memory(radar2), "same name, other frequency");
    check(load_memories().size() == 3, "three memories");

    check(remove_memory(128150000, "Luqa Radar"), "remove by (hz, name)");
    check(!remove_memory(128150000, "Luqa Radar"), "remove twice fails");
    all = load_memories();
    check(all.size() == 2 && all[0].hz == 156500000 && all[1].hz == 118350000, "order kept after remove");

    // Bad entries are skipped, a bad file is an empty list.
    {
        std::ofstream out(memories_file(), std::ios::trunc);
        out << R"({"version": 1, "memories": [
            {"name": "ok", "hz": 518000, "mode": "USB", "tags": ["navtex"]},
            {"name": "", "hz": 1},
            {"hz": 2},
            {"name": "no hz"},
            {"name": "float", "hz": 1.5},
            {"name": "negative", "hz": -3},
            "not an object"
        ]})";
    }
    all = load_memories();
    check(all.size() == 1 && all[0].name == "ok" && all[0].hz == 518000, "bad entries skipped");
    {
        std::ofstream out(memories_file(), std::ios::trunc);
        out << "{ this is not json";
    }
    check(load_memories().empty(), "bad file -> empty list");
    {
        std::ofstream out(memories_file(), std::ios::trunc);
        out << "[1, 2, 3]";
    }
    check(load_memories().empty(), "wrong shape -> empty list");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (failures == 0) std::printf("memories_store_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
