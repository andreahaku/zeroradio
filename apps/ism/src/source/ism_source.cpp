/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ism_source.h"

#include "ism_reading.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef ISM_HAVE_SOURCE
#include "subprocess.h"
#endif

namespace ism {
namespace {

// Cap the partial-line buffer: a broken tool that never emits a newline must
// not grow the worker's buffer without bound. rtl_433 JSON lines are well
// under this; anything larger is junk and gets dropped.
constexpr size_t kMaxLineBuffer = 64 * 1024;

// Decode one JSON line into the store; returns true if it was a valid reading.
bool feed_line(toolkit::EntityStore& store, const std::string& line, std::atomic<bool>& healthy,
               std::atomic<bool>& seen) {
    IsmReading r;
    if (!parse_ism_json_line(line, r)) return false;
    apply_to_store(store, r);
    healthy.store(true, std::memory_order_relaxed);
    seen.store(true, std::memory_order_relaxed);
    return true;
}

} // namespace

struct IsmSource::Impl {
    explicit Impl(toolkit::EntityStore& s) : store(s) {}

    toolkit::EntityStore& store;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> healthy{false}; // decoding right now (drives the header dot)
    std::atomic<bool> seen{false};    // ever decoded a reading

    // Sleep up to `ms`, waking every 100 ms to honour a stop() so shutdown and
    // the respawn backoff never block the join() for the full interval.
    void interruptible_sleep(int ms) {
        for (int slept = 0; slept < ms && running.load(std::memory_order_relaxed); slept += 100)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Mode, resolved once in the constructor.
    enum class Mode { Rtl433, File, Mock } mode{Mode::Mock};
    std::string file_path;      // File mode
    std::string rtl_tcp;        // "host:port" when reading a networked dongle
    std::string extra_args;     // ISM_RTL433_ARGS, space-split at spawn

    void run_file() {
        std::ifstream in(file_path);
        if (!in) {
            std::fprintf(stderr, "ism: cannot open ISM_JSON file '%s'\n", file_path.c_str());
            return;
        }
        std::string line;
        while (running.load(std::memory_order_relaxed) && std::getline(in, line)) {
            feed_line(store, line, healthy, seen);
        }
    }

    void run_mock() {
        // A weather sensor with drifting temp/hum plus two static TPMS. Emitted
        // straight into the store, no subprocess — works with zero hardware.
        double temp = 21.0;
        int hum = 45;
        int tick = 0;
        while (running.load(std::memory_order_relaxed)) {
            temp += (tick % 2 == 0) ? 0.3 : -0.2;
            hum += (tick % 3 == 0) ? 1 : -1;
            char line[256];
            std::snprintf(line, sizeof(line),
                          "{\"model\":\"Nexus-TH\",\"id\":155,\"channel\":1,"
                          "\"battery_ok\":1,\"temperature_C\":%.1f,\"humidity\":%d,"
                          "\"rssi\":-6.5,\"snr\":13.0}",
                          temp, hum);
            feed_line(store, line, healthy, seen);
            feed_line(store,
                      "{\"model\":\"Toyota\",\"type\":\"TPMS\",\"id\":\"d75e2810\","
                      "\"pressure_PSI\":39.25,\"temperature_C\":32.0,\"rssi\":-3.1}",
                      healthy, seen);
            feed_line(store,
                      "{\"model\":\"Renault\",\"type\":\"TPMS\",\"id\":\"072f1e\","
                      "\"pressure_kPa\":279.8,\"temperature_C\":42.0,\"rssi\":-4.3}",
                      healthy, seen);
            interruptible_sleep(1000);
            ++tick;
        }
    }

#ifdef ISM_HAVE_SOURCE
    void run_rtl433() {
        std::vector<std::string> argv = {"rtl_433", "-F", "json"};
        if (!rtl_tcp.empty()) {
            argv.push_back("-d");
            argv.push_back("rtl_tcp:" + rtl_tcp);
        }
        // Split ISM_RTL433_ARGS on spaces (simple; quoting is not supported).
        size_t pos = 0;
        while (pos < extra_args.size()) {
            const size_t next = extra_args.find(' ', pos);
            const std::string tok = extra_args.substr(pos, next - pos);
            if (!tok.empty()) argv.push_back(tok);
            if (next == std::string::npos) break;
            pos = next + 1;
        }

        std::string buffer; // accumulates partial lines across reads
        while (running.load(std::memory_order_relaxed)) {
            toolkit::Subprocess child;
            if (!child.start(argv)) {
                healthy.store(false, std::memory_order_relaxed);
                interruptible_sleep(2000);
                continue;
            }
            char chunk[4096];
            while (running.load(std::memory_order_relaxed)) {
                const ssize_t n = child.read_stdout(chunk, sizeof(chunk));
                if (n < 0) break;            // EOF: child exited (Subprocess contract)
                if (n == 0) {                // nothing available yet
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                buffer.append(chunk, static_cast<size_t>(n));
                size_t nl;
                while ((nl = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, nl);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    feed_line(store, line, healthy, seen);
                    buffer.erase(0, nl + 1);
                }
                // Drop a pathological newline-less buffer instead of growing it.
                if (buffer.size() > kMaxLineBuffer) buffer.clear();
            }
            child.stop();
            buffer.clear();
            healthy.store(false, std::memory_order_relaxed); // the tool exited/was stopped
            if (running.load(std::memory_order_relaxed))
                interruptible_sleep(2000); // respawn backoff
        }
    }
#endif

    void run() {
        switch (mode) {
            case Mode::File: run_file(); break;
#ifdef ISM_HAVE_SOURCE
            case Mode::Rtl433: run_rtl433(); break;
#else
            case Mode::Rtl433: run_mock(); break;
#endif
            case Mode::Mock: run_mock(); break;
        }
    }
};

IsmSource::IsmSource(toolkit::EntityStore& store) : impl_(std::make_unique<Impl>(store)) {
    const char* json = std::getenv("ISM_JSON");
    const char* src = std::getenv("ISM_SOURCE");
    if (json && json[0] != '\0') {
        impl_->mode = Impl::Mode::File;
        impl_->file_path = json;
    } else if (src && std::strcmp(src, "mock") == 0) {
        impl_->mode = Impl::Mode::Mock;
    } else {
        impl_->mode = Impl::Mode::Rtl433;
        if (src && std::strncmp(src, "rtl_tcp:", 8) == 0) impl_->rtl_tcp = src + 8;
        if (const char* a = std::getenv("ISM_RTL433_ARGS"); a && a[0] != '\0')
            impl_->extra_args = a;
    }
}

IsmSource::~IsmSource() { stop(); }

void IsmSource::start() {
    if (impl_->running.exchange(true)) return;
    impl_->worker = std::thread([this] { impl_->run(); });
}

void IsmSource::stop() {
    if (!impl_->running.exchange(false)) return;
    if (impl_->worker.joinable()) impl_->worker.join();
}

// Current decode health: for the file/mock modes "seen" and "healthy" track
// together; for the live tool "healthy" drops when rtl_433 exits or is stopped.
bool IsmSource::ok() const { return impl_->healthy.load(std::memory_order_relaxed); }

} // namespace ism
