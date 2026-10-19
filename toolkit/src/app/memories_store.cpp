/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "memories_store.h"

#include "persisted_state.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace toolkit {
namespace {

constexpr int kVersion = 1;

using json = nlohmann::json;

// One entry of the "memories" array. Entries without a name or without a
// positive integer frequency are skipped.
bool parse_memory(const json& j, Memory& out) {
    if (!j.is_object()) return false;
    const auto name = j.find("name");
    const auto hz = j.find("hz");
    if (name == j.end() || !name->is_string() || name->get<std::string>().empty()) return false;
    if (hz == j.end() || !hz->is_number_integer() || hz->get<int64_t>() <= 0) return false;

    Memory m;
    m.name = name->get<std::string>();
    m.hz = hz->get<int64_t>();
    if (const auto mode = j.find("mode"); mode != j.end() && mode->is_string()) {
        m.mode = mode->get<std::string>();
    }
    if (const auto tags = j.find("tags"); tags != j.end() && tags->is_array()) {
        for (const auto& t : *tags) {
            if (t.is_string() && !t.get<std::string>().empty()) {
                m.tags.push_back(t.get<std::string>());
            }
        }
    }
    if (const auto note = j.find("note"); note != j.end() && note->is_string()) {
        m.note = note->get<std::string>();
    }
    out = std::move(m);
    return true;
}

json to_json(const Memory& m) {
    json j;
    j["name"] = m.name;
    j["hz"] = m.hz;
    j["mode"] = m.mode;
    j["tags"] = m.tags;
    if (!m.note.empty()) {
        j["note"] = m.note;
    }
    return j;
}

bool same_memory(const Memory& m, int64_t hz, const std::string& name) {
    return m.hz == hz && m.name == name;
}

} // namespace

std::filesystem::path memories_file() {
    return config_file("zeroradio", "memories.json");
}

std::vector<Memory> load_memories() {
    std::vector<Memory> out;
    const auto path = memories_file();
    if (path.empty()) return out;
    std::ifstream in(path);
    if (!in) return out;

    json doc;
    try {
        doc = json::parse(in);
    } catch (const json::exception& e) {
        std::fprintf(stderr, "[memories] %s: %s\n", path.c_str(), e.what());
        return out;
    }
    if (!doc.is_object()) return out;
    const auto list = doc.find("memories");
    if (list == doc.end() || !list->is_array()) return out;
    for (const auto& item : *list) {
        Memory m;
        if (parse_memory(item, m)) {
            out.push_back(std::move(m));
        }
    }
    return out;
}

bool save_memories(const std::vector<Memory>& all) {
    const auto path = memories_file();
    if (!ensure_parent_dir(path)) return false;

    json doc;
    doc["version"] = kVersion;
    json list = json::array();
    for (const auto& m : all) {
        list.push_back(to_json(m));
    }
    doc["memories"] = std::move(list);

    // Write next to the target, then rename over it: a reader (or a crash
    // mid-write) never sees a half-written file.
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << doc.dump(2) << '\n';
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool add_memory(const Memory& m) {
    auto all = load_memories();
    for (const auto& existing : all) {
        if (same_memory(existing, m.hz, m.name)) return false;
    }
    all.push_back(m);
    return save_memories(all);
}

bool remove_memory(int64_t hz, const std::string& name) {
    auto all = load_memories();
    const auto before = all.size();
    for (auto it = all.begin(); it != all.end();) {
        if (same_memory(*it, hz, name)) {
            it = all.erase(it);
        } else {
            ++it;
        }
    }
    if (all.size() == before) return false;
    return save_memories(all);
}

} // namespace toolkit
