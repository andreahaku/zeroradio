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
#include <chrono>
#include <cstdio>
#include <iterator>
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
    disarm_delete();
    if (direction == 0 || rows_.empty()) return;
    int row = selected_row_.value();
    if (row < 0) {
        selected_row_.set(first_memory_row());
        return;
    }
    for (row += direction; row >= 0 && row < static_cast<int>(rows_.size()); row += direction) {
        if (!rows_[static_cast<size_t>(row)].group) {
            selected_row_.set(row);
            bump_nav_refresh(); // the EDIT page shows the selected memory's mode
            return;
        }
    }
}

int64_t MemoriesViewModel::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void MemoriesViewModel::disarm_delete() {
    if (!delete_armed_) return;
    delete_armed_ = false;
    bump_nav_refresh(); // DEL? back to DEL
}

void MemoriesViewModel::tick(int64_t now_ms) {
    if (delete_armed_ && now_ms - delete_armed_at_ > kDeleteConfirmMs) disarm_delete();
}

const toolkit::Memory* MemoriesViewModel::selected_memory() const {
    const int row = selected_row_.value();
    if (row < 0 || row >= static_cast<int>(rows_.size())) return nullptr;
    const auto& selected = rows_[static_cast<size_t>(row)];
    if (selected.group) return nullptr;
    return &memories_[selected.memory_index];
}

void MemoriesViewModel::reload(int64_t hz, const std::string& name, int fallback_row,
                               int64_t old_hz, const std::string& old_name) {
    // Marks follow the identity; a renamed memory takes its mark along.
    std::vector<std::pair<int64_t, std::string>> marked;
    for (size_t i = 0; i < memories_.size(); ++i) {
        if (!marked_[i]) continue;
        if (memories_[i].hz == old_hz && memories_[i].name == old_name) {
            marked.emplace_back(hz, name);
        } else {
            marked.emplace_back(memories_[i].hz, memories_[i].name);
        }
    }
    memories_ = toolkit::load_memories();
    marked_.assign(memories_.size(), false);
    marked_count_ = 0;
    for (size_t i = 0; i < memories_.size(); ++i) {
        for (const auto& [mhz, mname] : marked) {
            if (memories_[i].hz == mhz && memories_[i].name == mname) {
                marked_[i] = true;
                ++marked_count_;
                break;
            }
        }
    }
    selected_row_.set(-1); // the old rows point into the old list
    rebuild_rows();
    int row = -1;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const auto& r = rows_[i];
        if (!r.group && memories_[r.memory_index].hz == hz &&
            memories_[r.memory_index].name == name) {
            row = static_cast<int>(i);
            break;
        }
    }
    if (row < 0) {
        // Gone (deleted): the memory row at the same place, or the one before.
        row = std::min(fallback_row, static_cast<int>(rows_.size()) - 1);
        while (row >= 0 && rows_[static_cast<size_t>(row)].group) --row;
        if (row < 0) row = first_memory_row();
    }
    selected_row_.set(row);
    bump_nav_refresh(); // the EDIT keys show the selected memory
    update_status();
}

void MemoriesViewModel::request_edit() {
    disarm_delete();
    const auto* m = selected_memory();
    if (!m) return;
    edit_draft_ = {m->hz, m->name, m->mode, m->tags.empty() ? std::string() : m->tags.front()};
    edit_request_.set(edit_request_.value() + 1);
}

void MemoriesViewModel::apply_edit(const std::string& name, const std::string& group) {
    // The change is applied to the memory as it is in the file, under the
    // store lock: what another app changed in it meanwhile stays.
    const auto result =
        toolkit::modify_memory(edit_draft_.hz, edit_draft_.name, [&](toolkit::Memory& m) {
            m.name = name;
            if (group.empty()) {
                if (!m.tags.empty()) m.tags.erase(m.tags.begin());
            } else if (m.tags.empty()) {
                m.tags.push_back(group);
            } else {
                m.tags.front() = group;
            }
        });
    const int row = selected_row_.value();
    switch (result) {
        case toolkit::EditResult::Saved:
            reload(edit_draft_.hz, name, row, edit_draft_.hz, edit_draft_.name);
            show_status("Saved: " + name);
            break;
        case toolkit::EditResult::NotFound:
            reload(0, {}, row);
            show_status("Not saved: the memory is gone");
            break;
        case toolkit::EditResult::Duplicate:
            show_status("Not saved: " + name + " is already on that frequency");
            break;
        case toolkit::EditResult::Invalid:
            show_status("Not saved: the name is empty");
            break;
        case toolkit::EditResult::WriteFailed:
            show_status("Not saved: cannot write the memories file");
            break;
    }
}

void MemoriesViewModel::cycle_mode() {
    disarm_delete();
    const auto* m = selected_memory();
    if (!m) return;
    const int64_t hz = m->hz;
    const std::string name = m->name;
    static constexpr const char* kModes[] = {"WFM", "NFM", "AM", "USB", "LSB", "CW"};
    // The next mode after the one in the file, not the cached one.
    const auto result = toolkit::modify_memory(hz, name, [](toolkit::Memory& x) {
        size_t next = 0; // an unknown or empty mode starts the cycle
        for (size_t i = 0; i < std::size(kModes); ++i) {
            if (x.mode == kModes[i]) {
                next = (i + 1) % std::size(kModes);
                break;
            }
        }
        x.mode = kModes[next];
    });
    const int row = selected_row_.value();
    if (result == toolkit::EditResult::Saved) {
        reload(hz, name, row);
    } else if (result == toolkit::EditResult::NotFound) {
        reload(0, {}, row);
        show_status("Not saved: the memory is gone");
    } else {
        show_status("Not saved: cannot write the memories file");
    }
}

void MemoriesViewModel::request_delete(int64_t now_ms) {
    const auto* m = selected_memory();
    if (!m) return;
    const bool confirmed = delete_armed_ && delete_hz_ == m->hz && delete_name_ == m->name &&
                           now_ms - delete_armed_at_ <= kDeleteConfirmMs;
    if (!confirmed) {
        delete_armed_ = true;
        delete_hz_ = m->hz;
        delete_name_ = m->name;
        delete_armed_at_ = now_ms;
        bump_nav_refresh();
        show_status("Press 7 again to delete: " + m->name);
        return;
    }
    disarm_delete();
    const std::string name = m->name;
    if (!toolkit::remove_memory(delete_hz_, delete_name_)) {
        // Gone already, or the file could not be written: the reload tells.
        const auto now = toolkit::load_memories();
        const bool gone = std::none_of(now.begin(), now.end(), [&](const toolkit::Memory& x) {
            return x.hz == delete_hz_ && x.name == delete_name_;
        });
        reload(delete_hz_, delete_name_, selected_row_.value());
        show_status(gone ? "Already deleted: " + name : "Not deleted: cannot write the memories file");
        return;
    }
    reload(0, {}, selected_row_.value());
    show_status("Deleted: " + name);
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
    disarm_delete();
    sort_by_frequency_ = !sort_by_frequency_;
    rebuild_rows();
}

void MemoriesViewModel::request_search() {
    disarm_delete();
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
    } else if (page == 2) {
        const auto* m = selected_memory();
        mode_label_ = m ? (m->mode.empty() ? std::string("-") : m->mode) : std::string("-");
        out[1] = {view::ICON_KEYBOARD, false, m != nullptr};           // rename / regroup
        out[2] = {mode_label_.c_str(), true, m != nullptr};            // next mode
        out[3] = {delete_armed_ ? "DEL?" : "DEL", true, m != nullptr}; // delete (twice)
        out[4] = {"", true, false};
    } else {
        const bool selected = selected_row_.value() >= 0;
        out[1] = {view::ICON_CHECK, false, selected};
        out[2] = {view::ICON_BROADCAST, false, selected};
        out[3] = {view::ICON_BAND, false, selected};
        out[4] = {view::ICON_MINUS, false, marked_count_ > 0};
    }
}

void MemoriesViewModel::nav_activate(int page, int slot) {
    if (page == 2) {
        if (slot == 1) request_edit();
        else if (slot == 2) cycle_mode();
        else if (slot == 3) {
            request_delete(now_ms());
        }
        return;
    }
    disarm_delete();
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
    return page == 0 ? "MEM" : (page == 1 ? "SCAN" : "EDIT");
}

toolkit::i18n::Text MemoriesViewModel::nav_hint(int page, int slot) const {
    namespace h = toolkit::hints;
    if (page == 2) {
        switch (slot) {
            case 1: return {"Rename this memory or change its group",
                            "Rinomina questa memoria o cambiane il gruppo",
                            "Renommer cette mémoire ou changer son groupe",
                            "Diesen Speicher umbenennen oder seine Gruppe ändern",
                            "Renombra esta memoria o cambia su grupo",
                            "このメモリーの名前やグループを変更", "重命名此存储频道或更改其分组"};
            case 2: return {"Switch this memory to the next mode",
                            "Passa questa memoria al modo successivo",
                            "Passe cette mémoire au mode suivant",
                            "Diesen Speicher auf die nächste Betriebsart stellen",
                            "Cambia esta memoria al modo siguiente",
                            "このメモリーを次のモードに切り替え", "将此存储频道切换到下一种模式"};
            case 3: return {"Delete this memory: press twice to confirm",
                            "Cancella questa memoria: premi due volte per confermare",
                            "Supprimer cette mémoire : appuyer deux fois pour confirmer",
                            "Diesen Speicher löschen: zum Bestätigen zweimal drücken",
                            "Borra esta memoria: pulsa dos veces para confirmar",
                            "このメモリーを削除（2 回押して確定）", "删除此存储频道：按两次确认"};
            default: return {};
        }
    }
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
