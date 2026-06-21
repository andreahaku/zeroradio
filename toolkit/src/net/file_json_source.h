/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace toolkit {

// Background poller for a local JSON file (e.g. dump1090's aircraft.json). Every
// `poll_ms` it reads the whole file and invokes `on_json` ON THE READER THREAD;
// the app parses it and applies the result to a (locked) EntityStore. The UI
// never blocks. Lifecycle mirrors RtlTcpSource: an atomic stop flag, short sleep
// steps so stop is observed quickly, and a join in the destructor.
class FileJsonSource {
public:
    FileJsonSource(std::string path,
                   std::function<void(const std::string&)> on_json,
                   int poll_ms = 2000);
    ~FileJsonSource();

    FileJsonSource(const FileJsonSource&) = delete;
    FileJsonSource& operator=(const FileJsonSource&) = delete;

    void start();
    void stop();

    // True while the most recent poll read the file successfully (it exists, is
    // readable, within the size cap, and the callback didn't throw). Clears if a
    // later poll fails, so the UI's connection indicator tracks live health.
    bool ok() const;

private:
    void run();
    bool read_file(std::string& out) const;

    std::string path_;
    std::function<void(const std::string&)> on_json_;
    int poll_ms_;

    std::atomic<bool> running_{false};
    std::atomic<bool> ok_{false};
    std::thread thread_;
};

} // namespace toolkit
