/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "memories_viewmodel.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

std::string scan_payload(const memories::MemoriesViewModel& vm) {
    for (const auto& [key, value] : vm.handoff().env) {
        if (key == "SDR_SCAN") return value;
    }
    return {};
}

} // namespace

int main() {
    char temp[] = "/tmp/zeroradio-memories-scan-XXXXXX";
    if (!::mkdtemp(temp) || ::setenv("XDG_CONFIG_HOME", temp, 1) != 0) return 1;

    std::vector<toolkit::Memory> items = {
        {"Beta", 1000000, "AM", {"group"}, ""},
        {"Alpha", 2000000, "NFM", {"group"}, ""},
        {"Charlie", 3000000, "", {"other"}, ""},
    };
    check(toolkit::save_memories(items), "save small list");
    {
        memories::MemoriesViewModel vm;
        vm.toggle_mark(); // Alpha: list order is by name within the group.
        vm.move_selection(1);
        vm.toggle_mark(); // Beta.
        vm.toggle_sort();
        check(vm.rows()[1].marked && vm.rows()[2].marked, "marks survive sorting");
        vm.set_query("Charlie");
        check(vm.rows().size() == 2, "search hides marked rows");
        vm.prepare_scan();
        check(vm.scan_count() == 2 && vm.scan_uses_marks(), "hidden marks take precedence");
        vm.confirm_scan();
        check(scan_payload(vm) == "1000000:AM,2000000:NFM",
              "marked payload follows frequency order");
    }
    {
        memories::MemoriesViewModel vm;
        vm.set_query("Charlie");
        vm.prepare_scan();
        vm.confirm_scan();
        check(scan_payload(vm) == "3000000:", "empty mode keeps the SDR mode");
    }

    items.clear();
    for (int i = 0; i < 70; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "M%02d", i);
        items.push_back({name, 1000000 + (70 - i) * 1000, "CW", {"large"}, ""});
    }
    check(toolkit::save_memories(items), "save large group");
    {
        memories::MemoriesViewModel vm;
        vm.set_query("M00");
        vm.prepare_scan();
        check(vm.scan_total() == 70 && vm.scan_count() == 64,
              "group scan includes hidden entries and shows the 64-entry limit");
        vm.confirm_scan();
        const std::string payload = scan_payload(vm);
        check(payload.rfind("1001000:CW,", 0) == 0, "group scan starts at lowest frequency");
        check(payload.find("1064000:CW") != std::string::npos &&
                  payload.find("1065000:CW") == std::string::npos,
              "group scan stops after 64 frequencies");
    }
    {
        memories::MemoriesViewModel vm;
        for (int i = 0; i < 65; ++i) {
            vm.toggle_mark();
            if (i < 64) vm.move_selection(1);
        }
        check(!vm.rows()[65].marked, "65th memory cannot be marked");
        vm.prepare_scan();
        check(vm.scan_count() == 64 && vm.scan_uses_marks(), "mark limit feeds scan");
        vm.cancel_scan();
        check(!vm.handoff().pending(), "cancelled scan does not hand off");
    }

    std::filesystem::remove_all(temp);
    if (failures == 0) std::printf("memories_scan_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
