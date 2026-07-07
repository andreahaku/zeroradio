/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <sys/types.h>
#include <vector>

namespace survey {

// A child process with its stdout captured through a non-blocking pipe.
// Start/read/stop only — respawn policy and stderr belong to the caller
// (stderr passes through to the parent's, where the tools print status).
class Subprocess {
public:
    Subprocess() = default;
    ~Subprocess();

    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;

    // fork/exec argv[0] with the given arguments. False when the fork/pipe
    // fails (exec failure surfaces as an immediate EOF on read()).
    bool start(const std::vector<std::string>& argv);

    // Non-blocking stdout read: >0 bytes read, 0 nothing available yet,
    // -1 EOF (child exited or closed its stdout).
    ssize_t read_stdout(char* buf, size_t len);

    // SIGTERM + reap. Safe to call repeatedly.
    void stop();

    bool running() const { return pid_ > 0; }

private:
    pid_t pid_ = -1;
    int stdout_fd_ = -1;
};

} // namespace survey
