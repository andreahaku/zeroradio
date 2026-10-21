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

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

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

// An exclusive lock on memories.json.lock for one load-modify-save cycle, so
// two writers never lose each other's change. Released on destruction. If the
// lock file cannot be opened the update still runs, unlocked.
class FileLock {
public:
    explicit FileLock(const std::filesystem::path& target) {
        if (!ensure_parent_dir(target)) return;
        const auto path = target.string() + ".lock";
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd_ >= 0) ::flock(fd_, LOCK_EX);
    }
    ~FileLock() {
        if (fd_ >= 0) ::close(fd_); // closing releases the lock
    }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    int fd_ = -1;
};

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

    // Write a temporary file next to the target, flush it to storage, then
    // rename it over the target: a reader, a crash or a power cut never leaves
    // a half-written file. The name is unique per process, so two writers never
    // share a temporary file.
    const auto tmp = path.string() + "." + std::to_string(::getpid()) + ".tmp";
    const std::string text = doc.dump(2) + "\n";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    bool ok = true;
    for (size_t done = 0; ok && done < text.size();) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n > 0) done += static_cast<size_t>(n);
        else ok = false;
    }
    ok = ::fsync(fd) == 0 && ok;
    ok = ::close(fd) == 0 && ok;
    std::error_code ec;
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool add_memory(const Memory& m) {
    // Same rules as the loader: an entry it would skip is refused here.
    if (m.name.empty() || m.hz <= 0) return false;
    FileLock lock(memories_file());
    auto all = load_memories();
    for (const auto& existing : all) {
        if (same_memory(existing, m.hz, m.name)) return false;
    }
    all.push_back(m);
    return save_memories(all);
}

namespace {

std::string mhz_text(int64_t hz) {
    char buf[24];
    if (hz % 1000 == 0) {
        std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(hz) / 1e6);
    } else {
        std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(hz) / 1e6);
    }
    return buf;
}

} // namespace

std::string memory_label(int64_t hz, const std::string& mode) {
    return mhz_text(hz) + " " + mode;
}

std::string memory_info(int64_t hz, const std::string& mode) {
    return mhz_text(hz) + " MHz  " + mode;
}

std::string guess_mode(int64_t hz) {
    if (hz >= 87500000 && hz < 108000000) return "WFM";
    if (hz >= 108000000 && hz < 137000000) return "AM";
    return "NFM";
}

std::string save_draft(const MemoryDraft& draft, const std::string& name,
                       const std::string& group) {
    for (const auto& m : load_memories()) {
        if (same_memory(m, draft.hz, name)) {
            return "Not saved: " + name + " is already on " + mhz_text(draft.hz);
        }
    }
    Memory m{name, draft.hz, draft.mode, {}, ""};
    if (!group.empty()) m.tags.push_back(group);
    if (!add_memory(m)) return "Not saved: cannot write the memories file";
    return "Saved: " + name;
}

std::optional<Memory> find_memory(int64_t hz, const std::string& mode) {
    for (auto& m : load_memories()) {
        if (m.hz == hz && m.mode == mode) return std::move(m);
    }
    return std::nullopt;
}

bool remove_memory(int64_t hz, const std::string& name) {
    FileLock lock(memories_file());
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
