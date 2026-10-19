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
#include <string_view>

namespace memories {
namespace {

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

constexpr size_t kMaxScanMemories = 64;

std::string group_for(const toolkit::Memory& memory) {
    return memory.tags.empty() ? "other" : memory.tags.front();
}

std::string_view scan_mode(std::string_view mode) {
    if (mode == "WFM" || mode == "NFM" || mode == "AM" || mode == "USB" ||
        mode == "LSB" || mode == "CW") return mode;
    return {};
}

} // namespace

MemoriesViewModel::MemoriesViewModel() : memories_(toolkit::load_memories()),
                                         marked_(memories_.size(), false) {
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
    const int old_row = selected_row_.value();
    const size_t selected_memory =
        old_row >= 0 && old_row < static_cast<int>(rows_.size()) &&
                !rows_[static_cast<size_t>(old_row)].group
            ? rows_[static_cast<size_t>(old_row)].memory_index
            : memories_.size();
    std::map<std::string, std::vector<size_t>> groups;
    const std::string needle = lowercase(query_);
    for (size_t i = 0; i < memories_.size(); ++i) {
        const auto& memory = memories_[i];
        if (!needle.empty() && lowercase(memory.name).find(needle) == std::string::npos) continue;
        groups[group_for(memory)].push_back(i);
    }

    rows_.clear();
    for (auto& [group, indices] : groups) {
        std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
            const auto& lhs = memories_[a];
            const auto& rhs = memories_[b];
            if (sort_by_frequency_ && lhs.hz != rhs.hz) return lhs.hz < rhs.hz;
            if (lhs.name != rhs.name) return lhs.name < rhs.name;
            return lhs.hz < rhs.hz;
        });
        rows_.push_back({true, group, "", "", 0, false});
        for (const size_t index : indices) {
            const auto& memory = memories_[index];
            rows_.push_back({false, memory.name, format_frequency(memory.hz), memory.mode,
                             index, marked_[index]});
        }
    }

    int selection = first_memory_row();
    if (selected_memory < memories_.size()) {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (!rows_[i].group && rows_[i].memory_index == selected_memory) {
                selection = static_cast<int>(i);
                break;
            }
        }
    }
    selected_row_.set(selection);
    rows_version_.set(rows_version_.value() + 1);
    bump_nav_refresh();
    update_status();
}

void MemoriesViewModel::update_status() {
    const size_t matches = static_cast<size_t>(std::count_if(
        rows_.begin(), rows_.end(), [](const Row& row) { return !row.group; }));
    char status[64];
    std::snprintf(status, sizeof(status), "%zu %s  |  %s", matches,
                  matches == 1 ? "memory" : "memories",
                  sort_by_frequency_ ? "frequency" : "name");
    std::string text = status;
    if (marked_count_) text += "  |  marked: " + std::to_string(marked_count_);
    if (!query_.empty()) text += "  |  find: " + query_;
    status_.set(text.c_str());
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

void MemoriesViewModel::toggle_mark() {
    const int row = selected_row_.value();
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    auto& selected = rows_[static_cast<size_t>(row)];
    if (selected.group) return;
    const size_t index = selected.memory_index;
    if (!marked_[index] && marked_count_ == kMaxScanMemories) {
        status_.set("Scan limit: 64 marked memories");
        return;
    }
    marked_[index] = !marked_[index];
    selected.marked = marked_[index];
    if (marked_[index]) ++marked_count_;
    else --marked_count_;
    rows_version_.set(rows_version_.value() + 1);
    bump_nav_refresh();
    update_status();
}

void MemoriesViewModel::clear_marks() {
    if (!marked_count_) return;
    std::fill(marked_.begin(), marked_.end(), false);
    marked_count_ = 0;
    for (auto& row : rows_) row.marked = false;
    rows_version_.set(rows_version_.value() + 1);
    bump_nav_refresh();
    update_status();
}

void MemoriesViewModel::prepare_scan(bool force_group) {
    const int row = selected_row_.value();
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const auto& selected = rows_[static_cast<size_t>(row)];
    if (selected.group) return;
    if (toolkit::find_sibling("sdr_app", "sdr").empty()) {
        status_.set("SDR app unavailable");
        return;
    }

    pending_scan_.clear();
    scan_uses_marks_ = !force_group && marked_count_ > 0;
    const std::string group = group_for(memories_[selected.memory_index]);
    for (size_t i = 0; i < memories_.size(); ++i) {
        if ((scan_uses_marks_ && marked_[i]) ||
            (!scan_uses_marks_ && group_for(memories_[i]) == group)) {
            pending_scan_.push_back(i);
        }
    }
    std::sort(pending_scan_.begin(), pending_scan_.end(), [this](size_t a, size_t b) {
        if (memories_[a].hz != memories_[b].hz) return memories_[a].hz < memories_[b].hz;
        return a < b;
    });
    pending_scan_total_ = pending_scan_.size();
    if (pending_scan_.size() > kMaxScanMemories) pending_scan_.resize(kMaxScanMemories);
    if (!pending_scan_.empty()) scan_request_.set(scan_request_.value() + 1);
}

void MemoriesViewModel::confirm_scan() {
    if (pending_scan_.empty()) return;
    std::string channels;
    for (size_t index : pending_scan_) {
        if (!channels.empty()) channels += ',';
        const auto& memory = memories_[index];
        channels += std::to_string(memory.hz);
        channels += ':';
        channels.append(scan_mode(memory.mode));
    }
    cancel_scan();
    request_handoff("sdr_app", "sdr", {{"SDR_SCAN", std::move(channels)}});
}

void MemoriesViewModel::cancel_scan() {
    pending_scan_.clear();
    pending_scan_total_ = 0;
}

void MemoriesViewModel::nav_fill(int page, NavSlot out[5]) const {
    out[0] = {"#", true, true};
    if (page == 0) {
        out[1] = {view::ICON_CARET_UP, false, true};
        out[2] = {view::ICON_CARET_DOWN, false, true};
        out[3] = {view::ICON_CHECK, false, selected_row_.value() >= 0};
        out[4] = {view::ICON_KEYBOARD, false, true};
    } else {
        const bool selected = selected_row_.value() >= 0;
        out[1] = {view::ICON_CHECK, false, selected};
        out[2] = {view::ICON_BROADCAST, false, selected};
        out[3] = {view::ICON_BAND, false, selected};
        out[4] = {view::ICON_MINUS, false, marked_count_ > 0};
    }
}

void MemoriesViewModel::nav_activate(int page, int slot) {
    if (page == 0) {
        if (slot == 1) move_selection(-1);
        else if (slot == 2) move_selection(1);
        else if (slot == 3) open_selected_in_sdr();
        else if (slot == 4) request_search();
    } else {
        if (slot == 1) toggle_mark();
        else if (slot == 2) prepare_scan();
        else if (slot == 3) prepare_scan(true);
        else if (slot == 4) clear_marks();
    }
}

const char* MemoriesViewModel::nav_page_name(int page) const {
    return page == 0 ? "MEM" : "SCAN";
}

toolkit::i18n::Text MemoriesViewModel::nav_hint(int page, int slot) const {
    namespace h = toolkit::hints;
    if (page == 1) {
        switch (slot) {
            case 1: return {"Mark or unmark this memory for scanning",
                            "Marca o smarca questa memoria per la scansione",
                            "Marquer ou démarquer cette mémoire pour le balayage",
                            "Diesen Speicher für den Suchlauf markieren oder abwählen",
                            "Marca o desmarca esta memoria para escanear",
                            "このメモリーのスキャン選択を切り替える", "标记或取消标记此存储频道以扫描"};
            case 2: return {"Scan marked memories, or this group if none are marked",
                            "Scansiona le memorie marcate, o questo gruppo se nessuna è marcata",
                            "Balayer les mémoires marquées, sinon ce groupe",
                            "Markierte Speicher suchen, sonst diese Gruppe",
                            "Escanea las memorias marcadas o este grupo si no hay marcas",
                            "選択済みメモリー、なければこのグループをスキャン", "扫描已标记的频道；未标记时扫描当前分组"};
            case 3: return {"Scan every memory in the current group",
                            "Scansiona tutte le memorie del gruppo corrente",
                            "Balayer toutes les mémoires du groupe actuel",
                            "Alle Speicher der aktuellen Gruppe durchsuchen",
                            "Escanea todas las memorias del grupo actual",
                            "現在のグループの全メモリーをスキャン", "扫描当前分组中的所有频道"};
            case 4: return {"Clear all scan marks",
                            "Cancella tutte le marcature di scansione",
                            "Effacer toutes les marques de balayage",
                            "Alle Suchlaufmarkierungen löschen",
                            "Borra todas las marcas de escaneo",
                            "すべてのスキャン選択を解除", "清除所有扫描标记"};
            default: return {};
        }
    }
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
