/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace toolkit {

// A helper tool the app keeps running for its own lifetime (e.g. rtl_tcp,
// readsb, AIS-catcher driving the dongle). A background thread starts it,
// drains its stdout (so a chatty tool never blocks on a full pipe), and
// respawns it with a fixed backoff if it exits. The destructor stops it, so the
// dongle is released when the app quits. Status only — the app talks to the
// tool through its own channel (socket, JSON file, UDP).
class ChildService {
public:
    explicit ChildService(std::vector<std::string> argv, int respawn_ms = 2000);
    ~ChildService();

    ChildService(const ChildService&) = delete;
    ChildService& operator=(const ChildService&) = delete;

private:
    void run();

    std::vector<std::string> argv_;
    int respawn_ms_;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

// Resolves a helper tool: the copy shipped next to this executable (bundled in
// the package) wins, else the bare name for a PATH lookup.
std::string find_tool(const std::string& name);

} // namespace toolkit
