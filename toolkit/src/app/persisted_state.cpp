/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "persisted_state.h"

#include <cstdlib>
#include <fstream>

namespace {
constexpr const char* kHubSubdir = "zeroradio/hub";
constexpr const char* kLastAppFile = "last_app";
} // namespace

namespace toolkit {

std::filesystem::path config_dir(const std::string& app_subdir) {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] != '\0') {
        return std::filesystem::path(xdg) / app_subdir;
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / app_subdir;
    }
    return {};
}

std::filesystem::path config_file(const std::string& app_subdir, const std::string& filename) {
    const auto dir = config_dir(app_subdir);
    return dir.empty() ? std::filesystem::path{} : dir / filename;
}

bool ensure_parent_dir(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return !ec;
}

void save_last_app(const std::string& id) {
    const auto path = config_file(kHubSubdir, kLastAppFile);
    if (!ensure_parent_dir(path)) return;
    std::ofstream out(path, std::ios::trunc);
    out << id << '\n';
}

std::string load_last_app() {
    std::ifstream in(config_file(kHubSubdir, kLastAppFile));
    std::string id;
    in >> id;
    return id;
}

} // namespace toolkit
