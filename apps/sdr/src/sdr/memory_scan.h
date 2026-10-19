/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sdr {

// One channel of a memory scan: a frequency and the SDR mode name to listen
// in ("AM", "NFM"...), empty to keep the current mode.
struct ScanChannel {
    int64_t hz = 0;
    std::string mode;
};

// The SDR_SCAN hand-off value from the Memories app: "<hz>:<MODE>,<hz>:<MODE>,...".
// The mode part is optional. Entries without a positive whole frequency are
// skipped, and the list stops at `max` entries.
std::vector<ScanChannel> parse_scan_list(const std::string& text, size_t max = 64);

// Scans a list of channels and stops on the first one with a signal. Pure: the
// caller feeds the time, the channel levels and the squelch state, and tunes
// the receiver to tune_hz().
//
// Channels that fit in one received frame form a group. While searching, the
// receiver sits on the group's centre and every channel of the group is read
// from the same frame, so a quiet group costs one settle time, not one per
// channel. A channel at or above the squelch level becomes the active channel:
// the receiver tunes to it and listens while the squelch is open. The channel
// is done for this pass when the squelch closes again (after its hold), when
// the squelch does not open within confirm_ms (a false start), or when the
// user skips it. The scan then reads the rest of the same group again, and
// moves to the next group once the group is quiet.
class MemoryScan {
public:
    struct Timing {
        int64_t settle_ms = 250;  // after a retune, before reading levels
        int64_t confirm_ms = 400; // on a channel, for the squelch to open
    };

    MemoryScan(std::vector<ScanChannel> channels, Timing timing);
    explicit MemoryScan(std::vector<ScanChannel> channels) : MemoryScan(std::move(channels), Timing{}) {}

    // Build the groups for frames `frame_span_hz` wide. `edge_hz` is the widest
    // half passband, kept clear of the frame edge. The scan goes on from the
    // group that holds the frequency it was on.
    void regroup(double frame_span_hz, int32_t edge_hz, int64_t now_ms);

    const std::vector<ScanChannel>& channels() const { return channels_; }
    size_t group_count() const { return groups_.size(); }
    size_t group_index() const { return group_; }

    // The frequency the receiver should be on now: the group centre while
    // searching, the active channel while listening.
    int64_t tune_hz() const;
    // The channel listened to, or -1 while searching.
    int active() const { return active_; }

    // Once per frame. `level_of(i)` is channel i's level (0..100) in the frame
    // just received, called only for channels of the current group. The scan
    // stops on a channel whose level is at least `threshold`.
    void tick(int64_t now_ms, const std::function<int(size_t)>& level_of, int threshold,
              bool squelch_open);

    void pause() { paused_ = true; }
    void resume() { paused_ = false; }
    bool paused() const { return paused_; }

    // Leave the active channel for the rest of this pass (the scan reads the
    // other channels of its group), or, while searching, leave the group.
    void skip(int64_t now_ms);
    // Leave the active channel out of the scan until it ends, and move on.
    // Returns false while searching, when there is no channel to lock out.
    bool lock_out(int64_t now_ms);
    bool locked_out(size_t i) const { return i < locked_.size() && locked_[i]; }
    // Every channel is locked out: nothing left to scan.
    bool exhausted() const;

private:
    struct Group {
        std::vector<size_t> members; // channel indices, by frequency
        int64_t centre_hz = 0;
    };
    void next_group(int64_t now_ms);
    void finish_channel(int64_t now_ms); // the active channel is done for this pass
    bool group_usable(size_t g) const;
    bool candidate(size_t i) const { return !locked_[i] && !done_[i]; }

    std::vector<ScanChannel> channels_;
    Timing timing_;
    std::vector<bool> locked_;
    std::vector<bool> done_;      // heard or skipped in this pass of the group
    std::vector<Group> groups_;
    size_t group_ = 0;
    int active_ = -1;
    bool paused_ = false;
    bool heard_ = false;          // the squelch opened on the active channel
    int64_t tuned_at_ms_ = 0;     // last retune
};

} // namespace sdr
