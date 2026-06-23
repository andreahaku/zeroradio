/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "file_json_source.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace toolkit {
namespace {

// Sleep granularity: short steps so a stop() is observed within ~50 ms even when
// the configured poll interval is long.
constexpr int kSleepStepMs = 50;

// Cap the file we'll load into memory. A real dump1090 aircraft.json is well
// under 1 MB even with hundreds of contacts; this bounds a corrupt/huge file so
// a single read can't exhaust the heap on a constrained device.
constexpr std::streamoff kMaxFileBytes = 8 * 1024 * 1024;

} // namespace

FileJsonSource::FileJsonSource(std::string path,
                               std::function<void(const std::string&)> on_json,
                               int poll_ms)
    : path_(std::move(path)),
      on_json_(std::move(on_json)),
      poll_ms_(poll_ms > 0 ? poll_ms : 2000) {}

FileJsonSource::~FileJsonSource() {
    stop();
}

void FileJsonSource::start() {
    if (running_.exchange(true)) {
        return; // already running
    }
    thread_ = std::thread([this] { run(); });
}

void FileJsonSource::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool FileJsonSource::ok() const {
    return ok_.load();
}

bool FileJsonSource::read_file(std::string& out) const {
    std::ifstream in(path_, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    const std::streamoff size = in.tellg();
    if (size < 0 || size > kMaxFileBytes) {
        return false; // unreadable size or larger than we'll load
    }
    in.seekg(0);
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

void FileJsonSource::run() {
    // Poll immediately on start, then every poll_ms, observing the stop flag in
    // short steps in between.
    while (running_.load()) {
        std::string json;
        if (!read_file(json)) {
            ok_.store(false); // file gone/unreadable/oversized: not healthy now
        } else {
            ok_.store(true);
            if (on_json_) {
                // Never let a parse/apply exception (e.g. std::bad_alloc) escape
                // the reader thread — an uncaught throw here would std::terminate
                // the whole process. Mark unhealthy and keep polling instead.
                try {
                    on_json_(json);
                } catch (...) {
                    ok_.store(false);
                }
            }
        }

        int waited = 0;
        while (running_.load() && waited < poll_ms_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kSleepStepMs));
            waited += kSleepStepMs;
        }
    }
}

} // namespace toolkit
