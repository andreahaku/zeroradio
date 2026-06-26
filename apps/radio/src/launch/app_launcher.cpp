/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "app_launcher.h"

#include "logger.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace radio {
namespace {

namespace fs = std::filesystem;

bool file_exists(const fs::path& path) {
    std::error_code ec;
    return !path.empty() && fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

// Directory of the running hub executable, resolved from /proc/self/exe.
fs::path hub_dir() {
    std::error_code ec;
    auto self = fs::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return self.parent_path();
}

} // namespace

std::string resolve_app_binary(const AppEntry& entry) {
    // 1. explicit override
    if (const char* dir = std::getenv("RADIO_APPS_DIR")) {
        auto candidate = fs::path{dir} / entry.bin;
        if (file_exists(candidate)) {
            return candidate.string();
        }
    }

    const auto dir = hub_dir();
    if (!dir.empty()) {
        // 2. install layout: every app binary sits next to the hub
        if (auto colocated = dir / entry.bin; file_exists(colocated)) {
            return colocated.string();
        }
        // 3. dev CMake tree: build/<preset>/apps/<app_dir>/<config>/<bin>,
        //    with the hub itself at build/<preset>/apps/radio/<config>/radio_app.
        for (const char* config : {"Debug", "Release", "RelWithDebInfo", "."}) {
            auto candidate = dir / ".." / ".." / entry.app_dir / config / entry.bin;
            if (file_exists(candidate)) {
                return fs::weakly_canonical(candidate).string();
            }
        }
    }

    return {};
}

int run_app_binary(const std::string& path) {
    std::array<char*, 2> argv = {const_cast<char*>(path.c_str()), nullptr};

    pid_t pid = 0;
    const int rc = posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv.data(), environ);
    if (rc != 0) {
        LOG_ERROR("failed to spawn {}: {}", path, rc);
        return -1;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        // retry on EINTR
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

} // namespace radio
