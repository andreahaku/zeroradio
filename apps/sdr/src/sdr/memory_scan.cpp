/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "memory_scan.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <numeric>
#include <sstream>

namespace sdr {

std::vector<ScanChannel> parse_scan_list(const std::string& text, size_t max) {
    std::vector<ScanChannel> out;
    std::stringstream in(text);
    for (std::string item; out.size() < max && std::getline(in, item, ',');) {
        const auto colon = item.find(':');
        const std::string hz_text = item.substr(0, colon);
        if (hz_text.empty() || hz_text.find_first_not_of("0123456789") != std::string::npos) continue;
        errno = 0;
        const long long hz = std::strtoll(hz_text.c_str(), nullptr, 10);
        if (errno != 0 || hz <= 0) continue;
        ScanChannel ch;
        ch.hz = hz;
        if (colon != std::string::npos) ch.mode = item.substr(colon + 1);
        out.push_back(std::move(ch));
    }
    return out;
}

MemoryScan::MemoryScan(std::vector<ScanChannel> channels, Timing timing)
    : channels_(std::move(channels)), timing_(timing), locked_(channels_.size(), false) {
    regroup(0.0, 0, 0); // one channel per group until the caller knows the frame span
}

void MemoryScan::regroup(double frame_span_hz, int32_t edge_hz, int64_t now_ms) {
    std::vector<size_t> order(channels_.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [this](size_t a, size_t b) { return channels_[a].hz < channels_[b].hz; });

    // Keep 10 % of the frame as margin: the display bins at the very edge are
    // the least reliable.
    const double usable = frame_span_hz * 0.9 - 2.0 * edge_hz;
    groups_.clear();
    for (size_t i = 0; i < order.size();) {
        Group g;
        const int64_t first = channels_[order[i]].hz;
        size_t j = i;
        while (j < order.size() &&
               (j == i || static_cast<double>(channels_[order[j]].hz - first) <= usable)) {
            g.members.push_back(order[j]);
            ++j;
        }
        g.centre_hz = (first + channels_[order[j - 1]].hz) / 2;
        groups_.push_back(std::move(g));
        i = j;
    }
    // Restart where the scan was, on the group that now holds that channel.
    group_ = std::min(group_, groups_.empty() ? size_t{0} : groups_.size() - 1);
    active_ = -1;
    heard_ = false;
    tuned_at_ms_ = now_ms;
}

int64_t MemoryScan::tune_hz() const {
    if (active_ >= 0) return channels_[static_cast<size_t>(active_)].hz;
    if (groups_.empty()) return 0;
    return groups_[group_].centre_hz;
}

bool MemoryScan::group_usable(size_t g) const {
    for (const size_t i : groups_[g].members)
        if (!locked_[i]) return true;
    return false;
}

bool MemoryScan::exhausted() const {
    return std::all_of(locked_.begin(), locked_.end(), [](bool b) { return b; });
}

void MemoryScan::next_group(int64_t now_ms) {
    active_ = -1;
    heard_ = false;
    tuned_at_ms_ = now_ms;
    if (groups_.empty() || exhausted()) return;
    for (size_t step = 0; step < groups_.size(); ++step) {
        group_ = (group_ + 1) % groups_.size();
        if (group_usable(group_)) return;
    }
}

void MemoryScan::tick(int64_t now_ms, const std::function<int(size_t)>& level_of, int threshold,
                      bool squelch_open) {
    if (paused_ || groups_.empty() || exhausted()) return;
    if (now_ms < tuned_at_ms_) tuned_at_ms_ = now_ms; // clock went back
    if (now_ms - tuned_at_ms_ < timing_.settle_ms) return;

    if (active_ < 0) {
        if (!group_usable(group_)) {
            next_group(now_ms);
            return;
        }
        // Searching: read every channel of the group from this frame and stop
        // on the strongest one at or above the squelch level.
        int best = -1;
        int best_level = -1;
        for (const size_t i : groups_[group_].members) {
            if (locked_[i]) continue;
            const int level = level_of(i);
            if (level >= threshold && level > best_level) {
                best = static_cast<int>(i);
                best_level = level;
            }
        }
        if (best < 0) {
            next_group(now_ms);
            return;
        }
        const int64_t before = tune_hz();
        active_ = best;
        heard_ = false;
        // A one-channel group is already on the channel: no retune, no settle.
        if (tune_hz() != before) tuned_at_ms_ = now_ms;
        return;
    }

    // Listening on the active channel.
    if (squelch_open) {
        heard_ = true;
        return;
    }
    // Closed: either the transmission ended (the squelch held through the
    // pauses), or it never opened here (a false start).
    if (heard_ || now_ms - tuned_at_ms_ >= timing_.settle_ms + timing_.confirm_ms) {
        next_group(now_ms);
    }
}

void MemoryScan::skip(int64_t now_ms) { next_group(now_ms); }

bool MemoryScan::lock_out(int64_t now_ms) {
    if (active_ < 0) return false;
    locked_[static_cast<size_t>(active_)] = true;
    next_group(now_ms);
    return true;
}

} // namespace sdr
