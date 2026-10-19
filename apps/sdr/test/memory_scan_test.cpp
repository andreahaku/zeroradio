/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// sdr::MemoryScan and parse_scan_list(): the SDR_SCAN list, grouping channels
// that share a frame, stopping on a signal, holding while the squelch is open,
// false starts, skip, lockout and pause. Plus channel_level() with an offset.

#include "memory_scan.h"
#include "squelch.h"

#include <cstdio>
#include <map>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

// Levels by channel frequency; anything else is quiet.
struct Air {
    std::map<int64_t, int> level;
    std::function<int(size_t)> reader(const sdr::MemoryScan& scan) {
        return [this, &scan](size_t i) {
            const auto it = level.find(scan.channels()[i].hz);
            return it == level.end() ? 5 : it->second;
        };
    }
};

} // namespace

int main() {
    // --- parse_scan_list ---
    auto list = sdr::parse_scan_list("156500000:NFM,128150000:AM,518000,x:AM,-5:AM,,0:NFM");
    check(list.size() == 3, "three valid entries");
    if (list.size() == 3) {
        check(list[0].hz == 156500000 && list[0].mode == "NFM", "hz and mode");
        check(list[1].hz == 128150000 && list[1].mode == "AM", "second entry");
        check(list[2].hz == 518000 && list[2].mode.empty(), "entry without a mode");
    }
    std::string many;
    for (int i = 0; i < 80; ++i) many += std::to_string(100000000 + i * 25000) + ":NFM,";
    check(sdr::parse_scan_list(many).size() == 64, "capped at 64");
    check(sdr::parse_scan_list("").empty(), "empty text");

    // --- channel_level with an offset: 320 bins over 320 kHz, 1 kHz per bin ---
    std::vector<float> mags(320, 0.1f);
    mags[210] = 0.8f; // 50 kHz above the VFO
    check(sdr::channel_level(mags.data(), 320, 320000.0, -6000, 6000, 50000) == 80,
          "offset channel reads its own bins");
    check(sdr::channel_level(mags.data(), 320, 320000.0, -6000, 6000, 0) == 10,
          "the VFO channel does not see it");
    check(sdr::channel_level(mags.data(), 320, 320000.0, -6000, 6000, 400000) == 0,
          "a channel off the frame reads 0");

    // --- grouping: in a 600 kHz frame the marine channels (500 kHz apart at
    // most) share one group, the airband ones (750 kHz apart) do not ---
    const std::vector<sdr::ScanChannel> chans = {
        {156800000, "NFM"}, {156500000, "NFM"}, {156300000, "NFM"},
        {128150000, "AM"},  {127400000, "AM"},
    };
    sdr::MemoryScan scan(chans, {250, 400});
    check(scan.group_count() == 5, "one group per channel before the frame span is known");
    scan.regroup(600000.0, 6000, 0);
    // Sorted: 127.400 | 128.150 | 156.300 156.500 156.800
    check(scan.group_count() == 3, "marine channels share one frame, airband ones do not");
    check(scan.tune_hz() == 127400000, "a one-channel group sits on its channel");

    Air air;
    const int th = 40;
    int64_t t = 0;
    // Quiet everywhere: each group costs one settle time.
    scan.tick(t += 100, air.reader(scan), th, false);
    check(scan.tune_hz() == 127400000, "waits for the settle time");
    scan.tick(t += 200, air.reader(scan), th, false);
    check(scan.tune_hz() == 128150000, "quiet group: next group");
    scan.tick(t += 260, air.reader(scan), th, false);
    check(scan.tune_hz() == 156550000, "marine group: tuned to its centre");

    check(sdr::MemoryScan(chans).group_count() == 5, "constructor keeps one group per channel");
    {
        sdr::MemoryScan wide(chans);
        wide.regroup(1024000.0, 6000, 0);
        check(wide.group_count() == 2, "a 1 MHz frame holds both airband channels");
    }

    // Channel 10 (156.500) comes up: the scan tunes to it and listens.
    air.level[156500000] = 70;
    scan.tick(t += 260, air.reader(scan), th, false);
    check(scan.active() >= 0 && scan.tune_hz() == 156500000, "stops on the active channel");
    scan.tick(t += 100, air.reader(scan), th, false);
    check(scan.tune_hz() == 156500000, "settles on the channel before judging it");
    scan.tick(t += 200, air.reader(scan), th, true);  // squelch open
    scan.tick(t += 5000, air.reader(scan), th, true); // a long transmission
    check(scan.tune_hz() == 156500000, "stays while the squelch is open");
    scan.tick(t += 30, air.reader(scan), th, false); // squelch closed after its hold
    check(scan.active() < 0 && scan.tune_hz() == 127400000, "moves on when the squelch closes");

    // A false start: level over the threshold in the group read, but the
    // squelch never opens on the channel.
    sdr::MemoryScan fs({{156500000, "NFM"}, {156800000, "NFM"}}, {250, 400});
    fs.regroup(1024000.0, 6000, 0);
    Air burst;
    burst.level[156800000] = 60;
    int64_t u = 0;
    fs.tick(u += 260, burst.reader(fs), th, false);
    check(fs.tune_hz() == 156800000, "false start: tuned to the channel");
    fs.tick(u += 300, burst.reader(fs), th, false);
    check(fs.tune_hz() == 156800000, "false start: waits for the confirm time");
    fs.tick(u += 400, burst.reader(fs), th, false);
    check(fs.active() < 0, "false start: moves on when the squelch never opens");

    // Skip, lockout and pause.
    sdr::MemoryScan k({{100000000, "NFM"}, {200000000, "NFM"}}, {250, 400});
    k.regroup(1024000.0, 6000, 0);
    Air busy;
    busy.level[100000000] = 90;
    busy.level[200000000] = 90;
    int64_t v = 0;
    k.tick(v += 260, busy.reader(k), th, false);
    check(k.active() == 0, "stops on the busy first channel");
    check(!k.lock_out(v) || k.tune_hz() == 200000000, "lockout moves on");
    check(k.locked_out(0), "channel locked out");
    k.tick(v += 260, busy.reader(k), th, false);
    check(k.active() == 1, "stops on the second channel");
    k.skip(v);
    k.tick(v += 260, busy.reader(k), th, false);
    check(k.active() == 1, "the locked channel is never visited again");
    k.pause();
    k.skip(v);
    k.tick(v += 1000, busy.reader(k), th, false);
    check(k.active() < 0 && k.paused(), "paused: no stop while paused");
    k.resume();
    check(!k.lock_out(v), "nothing to lock out while searching");
    k.tick(v += 260, busy.reader(k), th, false);
    check(k.active() == 1, "resumes");
    check(k.lock_out(v) && k.exhausted(), "all channels locked out");
    const int64_t at = k.tune_hz();
    k.tick(v += 1000, busy.reader(k), th, false);
    check(k.tune_hz() == at, "an exhausted scan stays put");

    if (failures == 0) std::printf("memory_scan_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
