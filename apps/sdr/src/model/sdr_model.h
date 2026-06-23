/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace sdr {

// Demodulation mode shown in the SDR header.
enum class RadioMode {
    WFM = 0, // wide FM (broadcast)
    FM  = 1, // narrow FM
    AM  = 2,
    USB = 3,
    LSB = 4,
    CW  = 5,
};
constexpr int kRadioModeCount = 6;

const char* radio_mode_name(RadioMode mode);

// Audio demodulation passband, as offsets in Hz relative to the carrier (VFO).
struct Passband {
    int32_t low_hz;
    int32_t high_hz;
};
Passband mode_passband(RadioMode mode);

// Coarse/fine VFO tuning steps (Hz), per mode.
struct TuneSteps {
    int32_t coarse_hz;
    int32_t fine_hz;
};
TuneSteps mode_tune_steps(RadioMode mode);

// Preset bands the "band" key jumps between (centre frequency in kHz).
struct BandPreset {
    const char* name;
    int32_t     center_khz;
};
constexpr int kBandPresetCount = 4;
const BandPreset& band_preset(int index);

// SDR receiver state, persisted across runs. The generic shell state (dark mode,
// current page, NavBar tool page) lives in toolkit::ShellViewModel; this model
// owns only the radio-specific state. SdrViewModel bridges the two.
class SdrModel {
public:
    // Loads the persisted session (if any) so the app resumes where it left off.
    SdrModel();

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    // --- SDR state ---
    // VFO frequency in Hz; setter clamps to [0, INT32_MAX] and skips redundant writes.
    int32_t vfo_hz() const;
    void set_vfo_hz(int64_t hz);
    void tune_up();
    void tune_down();

    bool fine_tune() const;
    void toggle_fine_tune();
    int32_t tune_step_hz() const;

    RadioMode mode() const;
    void set_mode(RadioMode mode);
    void cycle_mode();
    Passband passband() const;

    int32_t span_khz() const;
    void zoom_in();   // narrower span
    void zoom_out();  // wider span

    void cycle_band(); // jump VFO to the next preset band centre

    // --- Visual options ---
    bool freq_grid() const;
    bool time_grid() const;
    bool peak_hold() const;
    void toggle_freq_grid();
    void toggle_time_grid();
    void toggle_peak_hold();

    // --- Audio ---
    int  volume() const;      // 0..100
    bool muted() const;
    void volume_up();         // +5
    void volume_down();       // -5
    void toggle_mute();

    // --- Receiver settings ---
    bool    gain_auto() const;
    int32_t gain_tenth_db() const;
    void    toggle_gain_auto();
    void    gain_up();        // +~3 dB (switches to manual)
    void    gain_down();      // -~3 dB (switches to manual)

private:
    void load_state();
    void save_state() const;

    bool dark_mode_ = true;
    int32_t vfo_hz_ = 145500000; // 145.500 MHz
    RadioMode mode_ = RadioMode::FM;
    bool fine_tune_ = false;
    int span_level_ = 2;       // index into the span ladder (see sdr_model.cpp)
    int band_index_ = 0;
    bool freq_grid_ = true;
    bool time_grid_ = true;
    bool peak_hold_ = false;
    int  volume_ = 50;
    bool muted_ = false;
    bool gain_auto_ = true;
    int32_t gain_tenth_db_ = 297; // ~29.7 dB
};

} // namespace sdr
