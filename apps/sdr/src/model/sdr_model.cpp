/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "sdr_model.h"

#include "persisted_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace sdr {
namespace {

// Per-app config location (reused toolkit helper): ~/.config/zeroradio/sdr/state.
constexpr const char* kStateSubdir = "zeroradio/sdr";
constexpr const char* kStateFile   = "state";

// State file format version. v4 (SDRTerminal's layout): vfo, mode, volume, mute,
// span, band, fine, dark, freq grid, time grid, peak hold, gain auto, gain.
// v5 appends the frequency correction in tenths of ppm, v6 the squelch on/off
// and level. Older files still load, with the newer fields at their defaults.
constexpr int kStateVersion = 6;
constexpr int kOldestStateVersion = 4;

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
        case RadioMode::NFM: return "NFM";
        case RadioMode::AM:  return "AM";
        case RadioMode::USB: return "USB";
        case RadioMode::LSB: return "LSB";
        case RadioMode::CW:  return "CW";
    }
    return "NFM";
}

TuneSteps mode_tune_steps(RadioMode mode) {
    switch (mode) {
        case RadioMode::WFM: return {100000, 10000};
        case RadioMode::NFM:  return {25000, 5000};
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
        case RadioMode::NFM:  return {-6000, 6000};
        case RadioMode::AM:  return {-4000, 4000};
        case RadioMode::USB: return {0, 2700};
        case RadioMode::LSB: return {-2700, 0};
        case RadioMode::CW:  return {-250, 250};
    }
    return {-6000, 6000};
}

SdrModel::SdrModel() {
    load_state();
    // Handoff from the survey app (and scripts): SDR_FREQ overrides the
    // persisted VFO with an initial centre frequency in Hz.
    if (const char* env = std::getenv("SDR_FREQ"); env && env[0] != '\0') {
        const long long hz = std::atoll(env);
        if (hz > 0) {
            set_vfo_hz(hz);
        }
    }
    // SDR_MODE (a mode name such as "AM" or "NFM", from the Memories app)
    // overrides the persisted mode. An unknown name keeps the last mode.
    if (const char* env = std::getenv("SDR_MODE"); env && env[0] != '\0') {
        for (int i = 0; i < kRadioModeCount; ++i) {
            const auto m = static_cast<RadioMode>(i);
            if (std::strcmp(env, radio_mode_name(m)) == 0) {
                set_mode(m);
                break;
            }
        }
    }
    // Consume both once. A hand-off execs in place and keeps the environment,
    // so without this a later TAB from the Scanner back to the SDR would jump
    // to the old frequency and mode again.
    ::unsetenv("SDR_FREQ");
    ::unsetenv("SDR_MODE");
}

bool SdrModel::dark_mode() const { return dark_mode_; }
void SdrModel::set_dark_mode(bool enabled) { dark_mode_ = enabled; }
void SdrModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
    save_state();
}

int32_t SdrModel::vfo_hz() const { return vfo_hz_; }

void SdrModel::set_vfo_hz(int64_t hz, bool persist) {
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
    if (persist) {
        saved_vfo_hz_ = v;
        save_state();
    }
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
void SdrModel::set_mode(RadioMode mode, bool persist) {
    mode_ = mode;
    if (persist) {
        saved_mode_ = mode;
        save_state();
    }
}

void SdrModel::persist() {
    saved_vfo_hz_ = vfo_hz_;
    saved_mode_ = mode_;
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

int SdrModel::ppm_tenths() const { return ppm_tenths_; }

void SdrModel::set_ppm_tenths(int tenths) {
    if (tenths > kMaxPpmTenths) tenths = kMaxPpmTenths;
    if (tenths < -kMaxPpmTenths) tenths = -kMaxPpmTenths;
    if (tenths == ppm_tenths_) return;
    ppm_tenths_ = tenths;
    save_state();
}
void SdrModel::ppm_up() { set_ppm_tenths(ppm_tenths_ + 1); }
void SdrModel::ppm_down() { set_ppm_tenths(ppm_tenths_ - 1); }

int64_t SdrModel::corrected_hz(int64_t hz) const {
    // A crystal that runs fast by e ppm tunes every frequency e ppm high, so
    // ask for hz / (1 + e) to land on hz. Same as librtlsdr's correction, with
    // tenths of ppm instead of whole ppm.
    if (ppm_tenths_ == 0 || hz <= 0) return hz;
    return std::llround(static_cast<double>(hz) / (1.0 + ppm_tenths_ * 1e-7));
}

bool SdrModel::squelch_on() const { return squelch_on_; }
int SdrModel::squelch_threshold() const { return squelch_threshold_; }
void SdrModel::toggle_squelch() {
    squelch_on_ = !squelch_on_;
    save_state();
}
void SdrModel::squelch_up() {
    squelch_on_ = true;
    squelch_threshold_ = std::min(100 - kSquelchStep, squelch_threshold_ + kSquelchStep);
    save_state();
}
void SdrModel::squelch_down() {
    squelch_on_ = true;
    squelch_threshold_ = std::max(kSquelchStep, squelch_threshold_ - kSquelchStep);
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
    if (!(in >> version) || version < kOldestStateVersion || version > kStateVersion) {
        return; // unknown/legacy format -> keep defaults
    }
    int32_t hz = 0;
    int mode_idx = 0;
    if (in >> hz >> mode_idx) {
        if (hz >= 0) {
            vfo_hz_ = hz;
            saved_vfo_hz_ = hz;
        }
        if (mode_idx >= 0 && mode_idx < kRadioModeCount) {
            mode_ = static_cast<RadioMode>(mode_idx);
            saved_mode_ = mode_;
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
    int ppm = 0;
    if (version >= 5 && in >> ppm && ppm >= -kMaxPpmTenths && ppm <= kMaxPpmTenths) {
        ppm_tenths_ = ppm;
    }
    int sq_on = 0, sq_level = 0;
    if (version >= 6 && in >> sq_on >> sq_level) {
        squelch_on_ = (sq_on != 0);
        if (sq_level >= kSquelchStep && sq_level <= 100 - kSquelchStep) {
            squelch_threshold_ = sq_level;
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
        out << kStateVersion << ' ' << saved_vfo_hz_ << ' ' << static_cast<int>(saved_mode_)
            << ' ' << volume_ << ' ' << (muted_ ? 1 : 0)
            << ' ' << span_level_ << ' ' << band_index_
            << ' ' << (fine_tune_ ? 1 : 0) << ' ' << (dark_mode_ ? 1 : 0)
            << ' ' << (freq_grid_ ? 1 : 0) << ' ' << (time_grid_ ? 1 : 0)
            << ' ' << (peak_hold_ ? 1 : 0)
            << ' ' << (gain_auto_ ? 1 : 0) << ' ' << gain_tenth_db_
            << ' ' << ppm_tenths_
            << ' ' << (squelch_on_ ? 1 : 0) << ' ' << squelch_threshold_ << '\n';
    }
}

} // namespace sdr
