/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "subprocess.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace toolkit {

Subprocess::~Subprocess() { stop(); }

bool Subprocess::start(const std::vector<std::string>& argv) {
    stop();
    if (argv.empty()) return false;

    // Build the argv array BEFORE fork: the child must only call async-signal-safe
    // functions between fork and exec (no heap allocation). The backing strings
    // in `argv` outlive the call, so the char* stay valid.
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    // O_CLOEXEC so the read-end never leaks into a later fork/exec (e.g. the
    // open-in-SDR handoff child).
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) return false;

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }
    if (pid == 0) {
        // Child: stdout -> pipe (dup2 clears CLOEXEC on the copy); stderr passes
        // through for tool diagnostics. Only async-signal-safe calls here.
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        ::execvp(args[0], args.data());
        _exit(127); // exec failed: parent sees EOF on the pipe
    }

    ::close(fds[1]);
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    pid_ = pid;
    stdout_fd_ = fds[0];
    return true;
}

ssize_t Subprocess::read_stdout(char* buf, size_t len) {
    if (stdout_fd_ < 0) return -1;
    const ssize_t n = ::read(stdout_fd_, buf, len);
    if (n > 0) return n;
    // No data yet (non-blocking) or interrupted: keep polling, don't treat as EOF.
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    return -1; // EOF or hard error: child gone
}

void Subprocess::stop() {
    if (stdout_fd_ >= 0) {
        ::close(stdout_fd_);
        stdout_fd_ = -1;
    }
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        // Give the tool a moment to exit, then force it so stop() cannot block
        // forever on a wedged child.
        for (int i = 0; i < 20; ++i) {
            if (::waitpid(pid_, nullptr, WNOHANG) != 0) break;
            ::usleep(10000); // 10 ms x 20 = 200 ms grace
            if (i == 19) {
                ::kill(pid_, SIGKILL);
                ::waitpid(pid_, nullptr, 0);
            }
        }
        pid_ = -1;
    }
}

} // namespace toolkit
