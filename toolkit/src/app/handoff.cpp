/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "handoff.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace toolkit {

std::string find_sibling(const std::string& bin, const std::string& app_dir) {
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    const auto dir = self.parent_path();
    if (const auto sibling = dir / bin; std::filesystem::exists(sibling, ec)) {
        return sibling.string();
    }
    // build tree: .../apps/<me>/<config>/<me_app> -> .../apps/<app_dir>/<config>/<bin>
    const auto build = dir.parent_path().parent_path() / app_dir / dir.filename() / bin;
    if (std::filesystem::exists(build, ec)) return build.string();
    return {};
}

int run_handoff(const ShellViewModel& shell) {
    const auto& h = shell.handoff();
    if (!h.pending()) return 0;
    const std::string path = find_sibling(h.bin, h.app_dir);
    if (path.empty()) {
        std::fprintf(stderr, "handoff: %s not found\n", h.bin.c_str());
        return 1;
    }
    for (const auto& [key, value] : h.env) ::setenv(key.c_str(), value.c_str(), 1);
    std::fflush(nullptr);
    ::execl(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
    std::perror("handoff: exec");
    return 1;
}

} // namespace toolkit
