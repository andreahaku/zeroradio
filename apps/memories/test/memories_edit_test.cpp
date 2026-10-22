/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// The Memories EDIT page: rename and regroup, the mode cycle, and the
// two-press delete with its timeout. Every change goes through the shared
// store, and the scan marks survive it.

#include "memories_viewmodel.h"

#include <cstdio>
#include <cstdlib>
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

const toolkit::Memory* stored(const std::string& name) {
    static std::vector<toolkit::Memory> all;
    all = toolkit::load_memories();
    for (const auto& m : all) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

} // namespace

int main() {
    char temp[] = "/tmp/zeroradio-memories-edit-XXXXXX";
    if (!::mkdtemp(temp) || ::setenv("XDG_CONFIG_HOME", temp, 1) != 0) return 1;

    check(toolkit::save_memories({
              {"Alpha", 1000000, "AM", {"group", "extra"}, "a note"},
              {"Beta", 2000000, "NFM", {"group"}, ""},
              {"Gamma", 3000000, "WFM", {"group"}, ""},
          }),
          "save small list");

    memories::MemoriesViewModel vm;
    // Rows: [group] Alpha Beta Gamma; the cursor starts on Alpha.
    check(vm.selected_row() == 1, "cursor on the first memory");

    // Mark Gamma for a scan: the mark must survive the edits below.
    vm.move_selection(1);
    vm.move_selection(1);
    vm.toggle_mark();
    vm.move_selection(-1);
    vm.move_selection(-1);

    // Rename and regroup Alpha. The first tag is the group; the other tags
    // and the note stay.
    vm.request_edit();
    check(vm.edit_draft().name == "Alpha" && vm.edit_draft().group == "group",
          "the dialog starts from the selected memory");
    vm.apply_edit("Alpha 2", "new");
    const auto* a = stored("Alpha 2");
    check(a && a->hz == 1000000 && a->tags.size() == 2 && a->tags[0] == "new" &&
              a->tags[1] == "extra" && a->note == "a note",
          "renamed, regrouped, other tags and note kept");
    check(!stored("Alpha"), "old name gone");
    check(vm.rows()[static_cast<size_t>(vm.selected_row())].name == "Alpha 2",
          "the cursor follows the edited memory");

    // Renaming a marked memory keeps its mark.
    vm.toggle_mark(); // Alpha 2
    vm.request_edit();
    vm.apply_edit("Alpha 3", "new");
    check(vm.rows()[static_cast<size_t>(vm.selected_row())].name == "Alpha 3" &&
              vm.rows()[static_cast<size_t>(vm.selected_row())].marked,
          "a renamed memory keeps its scan mark");
    vm.toggle_mark(); // unmark it again
    vm.request_edit();
    vm.apply_edit("Alpha 2", "new");

    // An empty group drops the first tag.
    vm.request_edit();
    vm.apply_edit("Alpha 2", "");
    a = stored("Alpha 2");
    check(a && a->tags.size() == 1 && a->tags[0] == "extra", "empty group drops the first tag");

    // Gamma is still marked.
    bool gamma_marked = false;
    for (const auto& row : vm.rows()) {
        if (!row.group && row.name == "Gamma") gamma_marked = row.marked;
    }
    check(gamma_marked, "scan marks survive an edit");

    // The mode cycle: AM -> USB -> ... -> WFM, saved at once.
    vm.cycle_mode();
    a = stored("Alpha 2");
    check(a && a->mode == "USB", "AM -> USB");

    // Delete: the first press arms, a second within the timeout deletes.
    vm.move_selection(1); // onto Beta (group "group")
    const auto beta_row = vm.rows()[static_cast<size_t>(vm.selected_row())].name;
    check(beta_row == "Beta", "cursor on Beta");
    vm.request_delete(1000);
    check(vm.delete_armed() && stored("Beta"), "first press arms, nothing deleted");
    vm.request_delete(1000 + memories::MemoriesViewModel::kDeleteConfirmMs + 1);
    check(vm.delete_armed() && stored("Beta"), "too late: armed again, still there");
    vm.move_selection(1);
    check(!vm.delete_armed(), "moving the cursor disarms");
    vm.move_selection(-1);
    vm.request_delete(9000);
    vm.tick(9000 + memories::MemoriesViewModel::kDeleteConfirmMs + 1);
    check(!vm.delete_armed(), "the arm expires on its own");
    vm.request_delete(15000);
    vm.toggle_sort();
    check(!vm.delete_armed(), "TAB disarms");
    vm.toggle_sort();
    vm.request_delete(16000);
    vm.nav_page_changed(0);
    check(!vm.delete_armed(), "a page switch disarms");
    vm.request_delete(20000);
    vm.request_delete(20500);
    check(!stored("Beta") && !vm.delete_armed(), "second press in time deletes");
    check(toolkit::load_memories().size() == 2, "two memories left");
    check(vm.selected_row() >= 0, "the cursor stays on a memory");

    if (failures == 0) std::printf("memories_edit_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
