/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "sdr_model.h"

#include "persisted_state.h"

#include <array>
#include <fstream>

namespace sdr {
namespace {

// Per-app config location (reused toolkit helper): ~/.config/cardputer_radio/sdr/state.
constexpr const char* kStateSubdir = "cardputer_radio/sdr";
constexpr const char* kStateFile   = "state";

// State file format version (matches SDRTerminal's v4 layout: vfo, mode, volume,
// mute, span, band, fine, dark, freq grid, time grid, peak hold, gain auto, gain).
constexpr int kStateVersion = 4;

// Span ladder in kHz, widest -> narrowest (2.0 MHz down to 0.1 MHz).
constexpr std::array<int32_t, 6> kSpanLadder = {2000, 1500, 1000, 500, 200, 100};

constexpr std::array<BandPreset, kBandPresetCount> kBands = {{
    {"FM",   98000},   // broadcast FM
    {"Air",  127000},  // airband
    {"2m",   145500},  // 2 m amateur
    {"70cm", 433500},  // 70 cm amateur
}};

} // namespace

const BandPreset& band_preset(int index) {
    if (index < 0 || index >= kBandPresetCount) {
        index = 0;
    }
    return kBands[static_cast<size_t>(index)];
}

const char* radio_mode_name(RadioMode mode) {
    switch (mode) {
        case RadioMode::WFM: return "WFM";
        case RadioMode::FM:  return "FM";
        case RadioMode::AM:  return "AM";
        case RadioMode::USB: return "USB";
        case RadioMode::LSB: return "LSB";
        case RadioMode::CW:  return "CW";
    }
    return "FM";
}

TuneSteps mode_tune_steps(RadioMode mode) {
    switch (mode) {
        case RadioMode::WFM: return {100000, 10000};
        case RadioMode::FM:  return {25000, 5000};
        case RadioMode::AM:  return {9000, 1000};
        case RadioMode::USB:
        case RadioMode::LSB: return {1000, 100};
        case RadioMode::CW:  return {500, 100};
    }
    return {1000, 100};
}

Passband mode_passband(RadioMode mode) {
    switch (mode) {
        case RadioMode::WFM: return {-90000, 90000};
        case RadioMode::FM:  return {-6000, 6000};
        case RadioMode::AM:  return {-4000, 4000};
        case RadioMode::USB: return {0, 2700};
        case RadioMode::LSB: return {-2700, 0};
        case RadioMode::CW:  return {-250, 250};
    }
    return {-6000, 6000};
}

SdrModel::SdrModel() {
    load_state();
}

bool SdrModel::dark_mode() const { return dark_mode_; }
void SdrModel::set_dark_mode(bool enabled) { dark_mode_ = enabled; }
void SdrModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
    save_state();
}

int32_t SdrModel::vfo_hz() const { return vfo_hz_; }

void SdrModel::set_vfo_hz(int64_t hz) {
    if (hz < 0) {
        hz = 0;
    } else if (hz > INT32_MAX) {
        hz = INT32_MAX; // ~2147 MHz, the int32 Hz ceiling
    }
    const auto v = static_cast<int32_t>(hz);
    if (v == vfo_hz_) {
        return; // no change -> skip the redundant state write
    }
    vfo_hz_ = v;
    save_state();
}

int32_t SdrModel::tune_step_hz() const {
    const auto steps = mode_tune_steps(mode_);
    return fine_tune_ ? steps.fine_hz : steps.coarse_hz;
}

void SdrModel::tune_up() {
    set_vfo_hz(static_cast<int64_t>(vfo_hz_) + tune_step_hz());
}

void SdrModel::tune_down() {
    set_vfo_hz(static_cast<int64_t>(vfo_hz_) - tune_step_hz());
}

bool SdrModel::fine_tune() const { return fine_tune_; }
void SdrModel::toggle_fine_tune() {
    fine_tune_ = !fine_tune_;
    save_state();
}

RadioMode SdrModel::mode() const { return mode_; }
void SdrModel::set_mode(RadioMode mode) {
    mode_ = mode;
    save_state();
}
void SdrModel::cycle_mode() {
    const int next = (static_cast<int>(mode_) + 1) % kRadioModeCount;
    set_mode(static_cast<RadioMode>(next));
}

Passband SdrModel::passband() const { return mode_passband(mode_); }

int32_t SdrModel::span_khz() const {
    return kSpanLadder[static_cast<size_t>(span_level_)];
}

void SdrModel::zoom_in() {
    if (span_level_ < static_cast<int>(kSpanLadder.size()) - 1) {
        ++span_level_;
        save_state();
    }
}

void SdrModel::zoom_out() {
    if (span_level_ > 0) {
        --span_level_;
        save_state();
    }
}

void SdrModel::cycle_band() {
    band_index_ = (band_index_ + 1) % kBandPresetCount;
    set_vfo_hz(static_cast<int64_t>(band_preset(band_index_).center_khz) * 1000);
}

bool SdrModel::freq_grid() const { return freq_grid_; }
bool SdrModel::time_grid() const { return time_grid_; }
bool SdrModel::peak_hold() const { return peak_hold_; }

void SdrModel::toggle_freq_grid() { freq_grid_ = !freq_grid_; save_state(); }
void SdrModel::toggle_time_grid() { time_grid_ = !time_grid_; save_state(); }
void SdrModel::toggle_peak_hold() { peak_hold_ = !peak_hold_; save_state(); }

int SdrModel::volume() const { return volume_; }
bool SdrModel::muted() const { return muted_; }

void SdrModel::volume_up() {
    volume_ = volume_ + 5 > 100 ? 100 : volume_ + 5;
    save_state();
}
void SdrModel::volume_down() {
    volume_ = volume_ - 5 < 0 ? 0 : volume_ - 5;
    save_state();
}
void SdrModel::toggle_mute() { muted_ = !muted_; save_state(); }

bool SdrModel::gain_auto() const { return gain_auto_; }
int32_t SdrModel::gain_tenth_db() const { return gain_tenth_db_; }

void SdrModel::toggle_gain_auto() { gain_auto_ = !gain_auto_; save_state(); }

void SdrModel::gain_up() {
    gain_auto_ = false;
    gain_tenth_db_ = gain_tenth_db_ + 30 > 496 ? 496 : gain_tenth_db_ + 30;
    save_state();
}
void SdrModel::gain_down() {
    gain_auto_ = false;
    gain_tenth_db_ = gain_tenth_db_ - 30 < 0 ? 0 : gain_tenth_db_ - 30;
    save_state();
}

void SdrModel::load_state() {
    const auto path = toolkit::config_file(kStateSubdir, kStateFile);
    if (path.empty()) {
        return;
    }
    std::ifstream in(path);
    if (!in) {
        return;
    }
    int version = 0;
    if (!(in >> version) || version != kStateVersion) {
        return; // unknown/legacy format -> keep defaults
    }
    int32_t hz = 0;
    int mode_idx = 0;
    if (in >> hz >> mode_idx) {
        if (hz >= 0) {
            vfo_hz_ = hz;
        }
        if (mode_idx >= 0 && mode_idx < kRadioModeCount) {
            mode_ = static_cast<RadioMode>(mode_idx);
        }
    }
    int vol = 0, mute = 0;
    if (in >> vol >> mute) {
        if (vol >= 0 && vol <= 100) {
            volume_ = vol;
        }
        muted_ = (mute != 0);
    }
    int span_lvl = 0, band_idx = 0;
    if (in >> span_lvl >> band_idx) {
        if (span_lvl >= 0 && span_lvl < static_cast<int>(kSpanLadder.size())) {
            span_level_ = span_lvl;
        }
        if (band_idx >= 0 && band_idx < kBandPresetCount) {
            band_index_ = band_idx;
        }
    }
    int fine = 0, dark = 0, fgrid = 0, tgrid = 0, peak = 0;
    if (in >> fine >> dark >> fgrid >> tgrid >> peak) {
        fine_tune_ = (fine != 0);
        dark_mode_ = (dark != 0);
        freq_grid_ = (fgrid != 0);
        time_grid_ = (tgrid != 0);
        peak_hold_ = (peak != 0);
    }
    int gauto = 1, gtenth = 0;
    if (in >> gauto >> gtenth) {
        gain_auto_ = (gauto != 0);
        if (gtenth >= 0 && gtenth <= 496) {
            gain_tenth_db_ = gtenth;
        }
    }
}

void SdrModel::save_state() const {
    const auto path = toolkit::config_file(kStateSubdir, kStateFile);
    if (!toolkit::ensure_parent_dir(path)) {
        return;
    }
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << kStateVersion << ' ' << vfo_hz_ << ' ' << static_cast<int>(mode_)
            << ' ' << volume_ << ' ' << (muted_ ? 1 : 0)
            << ' ' << span_level_ << ' ' << band_index_
            << ' ' << (fine_tune_ ? 1 : 0) << ' ' << (dark_mode_ ? 1 : 0)
            << ' ' << (freq_grid_ ? 1 : 0) << ' ' << (time_grid_ ? 1 : 0)
            << ' ' << (peak_hold_ ? 1 : 0)
            << ' ' << (gain_auto_ ? 1 : 0) << ' ' << gain_tenth_db_ << '\n';
    }
}

} // namespace sdr
