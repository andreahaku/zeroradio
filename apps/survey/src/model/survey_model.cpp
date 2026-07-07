/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "survey_model.h"

#include "persisted_state.h"

#include <algorithm>
#include <fstream>

namespace survey {
namespace {

constexpr const char* kStateSubdir = "cardputer_radio/survey";
constexpr const char* kStateFile   = "state";
constexpr int kStateVersion = 2;

// Bounds of the swept range (RTL-SDR tuner limits; hackrf_sweep goes higher).
constexpr int64_t kMinHz = 24000000;
constexpr int64_t kMaxHz = 1766000000;
constexpr int64_t kMinSpanHz = 1000000; // 1 MHz: below this, use the SDR app

// What the sweep tools accept as bin width.
constexpr int32_t kMinBinHz = 1000;
constexpr int32_t kMaxBinHz = 5000000;

constexpr int kCanvasBins = 320;

} // namespace

SurveyModel::SurveyModel() { load_state(); }

bool SurveyModel::dark_mode() const { return dark_mode_; }

void SurveyModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
    save_state();
}

int64_t SurveyModel::start_hz() const { return start_hz_; }
int64_t SurveyModel::stop_hz() const { return stop_hz_; }

int32_t SurveyModel::bin_hz() const {
    const int64_t bin = (stop_hz_ - start_hz_) / kCanvasBins;
    return static_cast<int32_t>(std::clamp<int64_t>(bin, kMinBinHz, kMaxBinHz));
}

void SurveyModel::zoom_in() {
    const int64_t centre = (start_hz_ + stop_hz_) / 2;
    const int64_t half = std::max((stop_hz_ - start_hz_) / 4, kMinSpanHz / 2);
    set_range(centre - half, centre + half);
}

void SurveyModel::zoom_out() {
    const int64_t centre = (start_hz_ + stop_hz_) / 2;
    const int64_t half = (stop_hz_ - start_hz_);
    set_range(centre - half, centre + half);
}

void SurveyModel::set_center_span(int64_t center_hz, int64_t span_hz) {
    const int64_t half = std::max<int64_t>(span_hz / 2, kMinSpanHz / 2);
    set_range(center_hz - half, center_hz + half);
}

void SurveyModel::set_range(int64_t start_hz, int64_t stop_hz) {
    // Clamp into [kMinHz, kMaxHz] and guarantee at least the minimum span without
    // ever pushing an edge past the tuner bounds (a window near kMaxHz slides
    // down rather than overflowing).
    start_hz = std::clamp(start_hz, kMinHz, kMaxHz - kMinSpanHz);
    stop_hz = std::clamp(stop_hz, start_hz + kMinSpanHz, kMaxHz);
    start_hz_ = start_hz;
    stop_hz_ = stop_hz;
    save_state();
}

void SurveyModel::load_state() {
    const auto path = toolkit::config_file(kStateSubdir, kStateFile);
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    int version = 0;
    if (!(in >> version) || version != kStateVersion) return;
    long long start = 0, stop = 0;
    int dark = 1;
    if (in >> start >> stop >> dark) {
        if (start >= kMinHz && stop <= kMaxHz && start < stop) {
            start_hz_ = start;
            stop_hz_ = stop;
        }
        dark_mode_ = dark != 0;
    }
}

void SurveyModel::save_state() const {
    const auto path = toolkit::config_file(kStateSubdir, kStateFile);
    if (!toolkit::ensure_parent_dir(path)) return;
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << kStateVersion << ' ' << static_cast<long long>(start_hz_) << ' '
            << static_cast<long long>(stop_hz_) << ' '
            << (dark_mode_ ? 1 : 0) << '\n';
    }
}

} // namespace survey
