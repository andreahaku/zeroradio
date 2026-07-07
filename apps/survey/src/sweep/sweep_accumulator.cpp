/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "sweep_accumulator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

namespace survey {
namespace {

// Full-token finite parse of one comma-separated field (spaces trimmed).
// Returns false for empty/partial tokens ("100abc") and non-finite values.
bool parse_num(const char* begin, const char* end, double* out) {
    while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) --end;
    if (begin == end) return false;
    const std::string tok(begin, end);
    char* consumed = nullptr;
    const double v = std::strtod(tok.c_str(), &consumed);
    if (consumed != tok.c_str() + tok.size()) return false;
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

} // namespace

struct SweepAccumulator::Impl {
    int64_t start;
    int64_t stop;

    std::string linebuf;                 // trailing partial line across feed()s
    std::map<double, float> pending;     // centre -> dBm (sorted; last wins)
    std::vector<std::pair<double, float>> frame; // latest published sweep
    int sweeps = 0;
    double prev_low = 0.0;
    bool have_prev = false;

    Impl(int64_t start_hz, int64_t stop_hz) : start(start_hz), stop(stop_hz) {}

    void publish() {
        frame.assign(pending.begin(), pending.end());
        pending.clear();
        ++sweeps;
    }

    // One complete line (no trailing '\n'). Returns sweeps published (0 or 1).
    int handle_line(const char* line, size_t len) {
        if (len > 0 && line[len - 1] == '\r') --len;

        // Split into comma-separated fields.
        std::vector<std::pair<const char*, const char*>> fields;
        const char* p = line;
        const char* const end = line + len;
        while (p <= end) {
            const char* comma = std::find(p, end, ',');
            fields.emplace_back(p, comma);
            if (comma == end) break;
            p = comma + 1;
        }
        if (fields.size() < 7) return 0;

        double hz_low = 0.0, hz_high = 0.0, hz_step = 0.0;
        if (!parse_num(fields[2].first, fields[2].second, &hz_low) ||
            !parse_num(fields[3].first, fields[3].second, &hz_high) ||
            !parse_num(fields[4].first, fields[4].second, &hz_step)) {
            return 0;
        }
        if (!(hz_low >= 0.0 && hz_step > 0.0 && hz_low < hz_high)) return 0;

        int published = 0;
        if (have_prev && hz_low < prev_low && !pending.empty()) {
            publish();
            published = 1;
        }
        prev_low = hz_low;
        have_prev = true;

        for (size_t i = 6; i < fields.size(); ++i) {
            double dbm = 0.0;
            if (!parse_num(fields[i].first, fields[i].second, &dbm)) dbm = kNoDataDbm;
            const double centre = hz_low + hz_step * (i - 6) + hz_step / 2.0;
            if (centre >= static_cast<double>(start) && centre < static_cast<double>(stop)) {
                pending[centre] = static_cast<float>(dbm);
            }
        }
        return published;
    }
};

SweepAccumulator::SweepAccumulator(int64_t start_hz, int64_t stop_hz)
    : impl_(std::make_unique<Impl>(start_hz, stop_hz)) {}

SweepAccumulator::~SweepAccumulator() = default;

int SweepAccumulator::feed(const char* data, size_t len) {
    int published = 0;
    impl_->linebuf.append(data, len);
    size_t pos = 0;
    for (;;) {
        const size_t nl = impl_->linebuf.find('\n', pos);
        if (nl == std::string::npos) break;
        published += impl_->handle_line(impl_->linebuf.data() + pos, nl - pos);
        pos = nl + 1;
    }
    impl_->linebuf.erase(0, pos);
    return published;
}

int SweepAccumulator::flush() {
    int published = 0;
    if (!impl_->linebuf.empty()) {
        published += impl_->handle_line(impl_->linebuf.data(), impl_->linebuf.size());
        impl_->linebuf.clear();
    }
    if (!impl_->pending.empty()) {
        impl_->publish();
        ++published;
    }
    return published;
}

int SweepAccumulator::sweeps() const { return impl_->sweeps; }

bool SweepAccumulator::has_frame() const { return !impl_->frame.empty(); }

bool SweepAccumulator::frame_dbm(float* dbm, int n_bins) const {
    if (impl_->frame.empty() || n_bins <= 0) return false;
    std::vector<double> sum(static_cast<size_t>(n_bins), 0.0);
    std::vector<int> count(static_cast<size_t>(n_bins), 0);
    const double width =
        static_cast<double>(impl_->stop - impl_->start) / n_bins;
    for (const auto& [centre, value] : impl_->frame) {
        int k = static_cast<int>((centre - static_cast<double>(impl_->start)) / width);
        k = std::clamp(k, 0, n_bins - 1);
        sum[static_cast<size_t>(k)] += value;
        ++count[static_cast<size_t>(k)];
    }
    for (int k = 0; k < n_bins; ++k) {
        dbm[k] = count[static_cast<size_t>(k)] > 0
                     ? static_cast<float>(sum[static_cast<size_t>(k)] / count[static_cast<size_t>(k)])
                     : kNoDataDbm;
    }
    return true;
}

std::vector<SweepPeak> SweepAccumulator::peaks(float threshold_db, int max_peaks) const {
    std::vector<SweepPeak> out;
    const auto& f = impl_->frame;
    if (f.size() < 3 || max_peaks <= 0) return out;

    std::vector<float> sorted;
    sorted.reserve(f.size());
    for (const auto& [centre, value] : f) sorted.push_back(value);
    std::sort(sorted.begin(), sorted.end());
    const size_t mid = sorted.size() / 2;
    const double median =
        (sorted.size() % 2 != 0)
            ? sorted[mid]
            : (static_cast<double>(sorted[mid - 1]) + sorted[mid]) / 2.0;

    for (size_t i = 1; i + 1 < f.size(); ++i) {
        const float v = f[i].second;
        if (v > f[i - 1].second && v >= f[i + 1].second &&
            static_cast<double>(v) >= median + threshold_db) {
            // Round half-to-even (llrint under the default FE_TONEAREST mode)
            // to match the frozen oracle's python round().
            out.push_back({static_cast<int64_t>(std::llrint(f[i].first)), v});
        }
    }
    std::sort(out.begin(), out.end(), [](const SweepPeak& a, const SweepPeak& b) {
        if (a.dbm != b.dbm) return a.dbm > b.dbm;
        return a.freq_hz < b.freq_hz;
    });
    if (out.size() > static_cast<size_t>(max_peaks)) out.resize(static_cast<size_t>(max_peaks));
    return out;
}

int64_t SweepAccumulator::start_hz() const { return impl_->start; }
int64_t SweepAccumulator::stop_hz() const { return impl_->stop; }

} // namespace survey
