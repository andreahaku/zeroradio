/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "persisted_state.h"

#include <cstdlib>

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

} // namespace toolkit
