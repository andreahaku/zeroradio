/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "child_service.h"

#include "subprocess.h"

#include <chrono>
#include <climits>
#include <unistd.h>

namespace toolkit {

ChildService::ChildService(std::vector<std::string> argv, int respawn_ms)
    : argv_(std::move(argv)), respawn_ms_(respawn_ms) {
    thread_ = std::thread([this] { run(); });
}

ChildService::~ChildService() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void ChildService::run() {
    using clock = std::chrono::steady_clock;
    Subprocess child;
    auto next_spawn = clock::now();
    char sink[512];

    while (running_.load()) {
        ssize_t n = 0;
        while ((n = child.read_stdout(sink, sizeof(sink))) > 0) {
        }
        if (n < 0) child.stop(); // EOF: the tool exited (or was never started)

        const auto now = clock::now();
        if (!child.running() && now >= next_spawn) {
            child.start(argv_);
            next_spawn = now + std::chrono::milliseconds(respawn_ms_);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    child.stop();
}

std::string find_tool(const std::string& name) {
    char exe[PATH_MAX];
    const ssize_t len = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        std::string dir(exe, static_cast<size_t>(len));
        dir.resize(dir.rfind('/') + 1);
        const std::string bundled = dir + name;
        if (::access(bundled.c_str(), X_OK) == 0) return bundled;
    }
    return name;
}

} // namespace toolkit
