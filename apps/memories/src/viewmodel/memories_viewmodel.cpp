/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "memories_viewmodel.h"

#include "handoff.h"
#include "nav_hints.h"
#include "ui_const.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <numeric>

namespace memories {
namespace {

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

MemoriesViewModel::MemoriesViewModel() : memories_(toolkit::load_memories()) {
    set_help_doc("docs/help/memories.md");
    set_nav_provider(this);
    set_title("MEMORIES");
    rebuild_rows();
}

std::string MemoriesViewModel::format_frequency(int64_t hz) {
    char text[32];
    if (hz < 1000000) {
        std::snprintf(text, sizeof(text), "%.3f kHz", static_cast<double>(hz) / 1000.0);
    } else {
        std::snprintf(text, sizeof(text), "%.3f MHz", static_cast<double>(hz) / 1000000.0);
    }
    return text;
}

void MemoriesViewModel::rebuild_rows() {
    // A group header precedes its entries. Only entries can hold the cursor.
    std::map<std::string, std::vector<size_t>> groups;
    const std::string needle = lowercase(query_);
    for (size_t i = 0; i < memories_.size(); ++i) {
        const auto& memory = memories_[i];
        if (!needle.empty() && lowercase(memory.name).find(needle) == std::string::npos) continue;
        const std::string group = memory.tags.empty() ? "other" : memory.tags.front();
        groups[group].push_back(i);
    }

    rows_.clear();
    size_t matches = 0;
    for (auto& [group, indices] : groups) {
        std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
            const auto& lhs = memories_[a];
            const auto& rhs = memories_[b];
            if (sort_by_frequency_ && lhs.hz != rhs.hz) return lhs.hz < rhs.hz;
            if (lhs.name != rhs.name) return lhs.name < rhs.name;
            return lhs.hz < rhs.hz;
        });
        rows_.push_back({true, group, "", "", 0});
        for (const size_t index : indices) {
            const auto& memory = memories_[index];
            rows_.push_back({false, memory.name, format_frequency(memory.hz), memory.mode, index});
            ++matches;
        }
    }

    selected_row_.set(first_memory_row());
    rows_version_.set(rows_version_.value() + 1);
    bump_nav_refresh();
    char status[64];
    std::snprintf(status, sizeof(status), "%zu memories  |  %s%s%s", matches,
                  sort_by_frequency_ ? "frequency" : "name",
                  query_.empty() ? "" : "  |  find: ", query_.c_str());
    status_.set(status);
}

int MemoriesViewModel::first_memory_row() const {
    for (size_t i = 0; i < rows_.size(); ++i)
        if (!rows_[i].group) return static_cast<int>(i);
    return -1;
}

void MemoriesViewModel::set_query(std::string query) {
    query_ = std::move(query);
    rebuild_rows();
}

void MemoriesViewModel::move_selection(int direction) {
    if (direction == 0 || rows_.empty()) return;
    int row = selected_row_.value();
    if (row < 0) {
        selected_row_.set(first_memory_row());
        return;
    }
    for (row += direction; row >= 0 && row < static_cast<int>(rows_.size()); row += direction) {
        if (!rows_[static_cast<size_t>(row)].group) {
            selected_row_.set(row);
            return;
        }
    }
}

void MemoriesViewModel::open_selected_in_sdr() {
    const int row = selected_row_.value();
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const auto& selected = rows_[static_cast<size_t>(row)];
    if (selected.group) return;
    const auto& memory = memories_[selected.memory_index];
    if (toolkit::find_sibling("sdr_app", "sdr").empty()) {
        std::fprintf(stderr, "[memories] sdr_app not found next to this binary\n");
        return;
    }
    request_handoff("sdr_app", "sdr", {{"SDR_FREQ", std::to_string(memory.hz)},
                                        {"SDR_MODE", memory.mode}});
}

void MemoriesViewModel::toggle_sort() {
    sort_by_frequency_ = !sort_by_frequency_;
    rebuild_rows();
}

void MemoriesViewModel::request_search() {
    search_request_.set(search_request_.value() + 1);
}

void MemoriesViewModel::nav_fill(int, NavSlot out[5]) const {
    out[0] = {"#", true, true};
    out[1] = {view::ICON_CARET_UP, false, true};
    out[2] = {view::ICON_CARET_DOWN, false, true};
    out[3] = {view::ICON_CHECK, false, selected_row_.value() >= 0};
    out[4] = {view::ICON_KEYBOARD, false, true};
}

void MemoriesViewModel::nav_activate(int, int slot) {
    if (slot == 1) move_selection(-1);
    else if (slot == 2) move_selection(1);
    else if (slot == 3) open_selected_in_sdr();
    else if (slot == 4) request_search();
}

const char* MemoriesViewModel::nav_page_name(int) const { return "MEM"; }

toolkit::i18n::Text MemoriesViewModel::nav_hint(int, int slot) const {
    namespace h = toolkit::hints;
    switch (slot) {
        case 1: return h::kListUp;
        case 2: return h::kListDown;
        case 3: return {"Open the selected memory in SDR",
                        "Apri la memoria selezionata in SDR",
                        "Ouvre la mémoire sélectionnée dans SDR",
                        "Öffnet den gewählten Speicher im SDR",
                        "Abre la memoria seleccionada en SDR",
                        "選択したメモリーを SDR で開く", "在 SDR 中打开所选记忆"};
        case 4: return {"Search memories by name",
                        "Cerca memorie per nome",
                        "Recherche les mémoires par nom",
                        "Speicher nach Namen suchen",
                        "Busca memorias por nombre",
                        "名前でメモリーを検索", "按名称搜索记忆"};
        default: return {};
    }
}

} // namespace memories
