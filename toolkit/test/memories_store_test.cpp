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

#include <sys/wait.h>
#include <unistd.h>

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
    check(!add_memory({"", 1000, "AM", {}, ""}), "empty name refused");
    check(!add_memory({"zero", 0, "AM", {}, ""}), "zero frequency refused");
    check(!add_memory({"negative", -5, "AM", {}, ""}), "negative frequency refused");
    {
        int leftovers = 0;
        for (const auto& e : std::filesystem::directory_iterator(memories_file().parent_path())) {
            if (e.path().extension() == ".tmp") ++leftovers;
        }
        check(leftovers == 0, "no temporary file left");
    }

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

    // find_memory(): the same frequency AND mode, whatever the name (the SDR
    // asks before offering to save the tuned channel).
    {
        const auto hit = find_memory(128150000, "AM");
        check(hit && hit->name == "Luqa Radar", "find by frequency and mode");
        check(!find_memory(128150000, "NFM"), "same frequency, other mode: not found");
        check(!find_memory(128150001, "AM"), "other frequency: not found");
    }

    // Labels for a memory saved from the SDR or the Scanner.
    check(memory_label(88700000, "WFM") == "88.700 WFM", "label: kHz precision");
    check(memory_label(118008300, "AM") == "118.0083 AM", "label: 8.33 kHz channel");
    check(memory_label(518000, "USB") == "0.518 USB", "label: below 1 MHz");
    check(memory_info(127400000, "AM") == "127.400 MHz  AM", "dialog info line");
    check(guess_mode(98500000) == "WFM", "FM broadcast band: WFM");
    check(guess_mode(127400000) == "AM", "airband: AM");
    check(guess_mode(108000000) == "AM", "airband starts at 108 MHz");
    check(guess_mode(87499999) == "NFM", "below the FM band: NFM");
    check(guess_mode(156800000) == "NFM", "marine VHF: NFM");

    // save_draft(): the dialog's name and group onto a draft, with the
    // message to show.
    {
        MemoryDraft d{99600000, "WFM"};
        check(save_draft(d, "Radio 99", "fm") == "Saved: Radio 99", "save a draft");
        const auto hit = find_memory(99600000, "WFM");
        check(hit && hit->name == "Radio 99" && hit->tags.size() == 1 && hit->tags[0] == "fm",
              "draft saved with its group as the first tag");
        check(save_draft(d, "Radio 99", "fm") == "Not saved: Radio 99 is already on 99.600",
              "same name and frequency: refused");
        check(save_draft({99700000, "WFM"}, "No group", "") == "Saved: No group", "empty group");
        const auto bare = find_memory(99700000, "WFM");
        check(bare && bare->tags.empty(), "empty group: no tag");
        check(remove_memory(99600000, "Radio 99") && remove_memory(99700000, "No group"),
              "drafts removed");
    }

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

    // Concurrent writers: every child adds its own memories at the same time.
    // The lock file must keep every update.
    std::filesystem::remove(memories_file());
    constexpr int kChildren = 8;
    constexpr int kEach = 10;
    for (int c = 0; c < kChildren; ++c) {
        if (fork() == 0) {
            int bad = 0;
            for (int i = 0; i < kEach; ++i) {
                const Memory m{"c" + std::to_string(c) + "-" + std::to_string(i),
                               100000000 + c * 1000 + i, "NFM", {"test"}, ""};
                if (!add_memory(m)) ++bad;
            }
            _exit(bad == 0 ? 0 : 1);
        }
    }
    bool children_ok = true;
    for (int c = 0; c < kChildren; ++c) {
        int status = 0;
        wait(&status);
        children_ok = children_ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    check(children_ok, "every concurrent add succeeded");
    check(load_memories().size() == kChildren * kEach, "no update lost under concurrent writers");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (failures == 0) std::printf("memories_store_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
