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
    NFM = 1, // narrow FM
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
    // `persist` false changes it for this session only (the memory scan retunes
    // several times a second and must not write the state file each time).
    int32_t vfo_hz() const;
    void set_vfo_hz(int64_t hz, bool persist = true);
    void tune_up();
    void tune_down();

    bool fine_tune() const;
    void toggle_fine_tune();
    int32_t tune_step_hz() const;

    RadioMode mode() const;
    void set_mode(RadioMode mode, bool persist = true);
    // Make the session-only frequency and mode the saved ones, and write the
    // state file (when a memory scan ends).
    void persist();
    void cycle_mode();
    Passband passband() const;

    int32_t span_khz() const;
    // `persist` false zooms for this session only (the calibration zooms in
    // and must not leave the narrowest span in the file if the power drops).
    void zoom_in(bool persist = true);   // narrower span
    void zoom_out(bool persist = true);  // wider span

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

    // --- Frequency correction ---
    // The dongle's crystal error in tenths of ppm, in the rtl-sdr sign
    // convention (the value `rtl_test -p` reports and `rtl_fm -p` takes).
    // Clamped to +/-kMaxPpmTenths.
    static constexpr int kMaxPpmTenths = 2000; // +/-200.0 ppm
    int  ppm_tenths() const;
    void set_ppm_tenths(int tenths);
    void ppm_up();            // +0.1 ppm
    void ppm_down();          // -0.1 ppm
    // The frequency to ask the tuner for so that `hz` lands on the VFO.
    int64_t corrected_hz(int64_t hz) const;

    // --- Squelch ---
    // On/off and the opening level on the S-meter scale (5..95). Turning it
    // off keeps the level for the next time.
    static constexpr int kSquelchStep = 5;
    bool squelch_on() const;
    int  squelch_threshold() const;
    void toggle_squelch();
    void squelch_up();        // +kSquelchStep
    void squelch_down();      // -kSquelchStep

private:
    void load_state();
    void save_state() const;

    bool dark_mode_ = true;
    int32_t vfo_hz_ = 145500000; // 145.500 MHz
    RadioMode mode_ = RadioMode::NFM;
    // What the state file holds. Equal to vfo_hz_/mode_ except during a memory
    // scan, whose session-only retunes must not reach the file when another
    // setting (volume, gain, zoom) saves the state.
    int32_t saved_vfo_hz_ = 145500000;
    RadioMode saved_mode_ = RadioMode::NFM;
    bool fine_tune_ = false;
    int span_level_ = 2;       // index into the span ladder (see sdr_model.cpp)
    int saved_span_level_ = 2; // what the state file holds, like saved_vfo_hz_
    int band_index_ = 0;
    bool freq_grid_ = true;
    bool time_grid_ = true;
    bool peak_hold_ = false;
    int  volume_ = 50;
    bool muted_ = false;
    bool gain_auto_ = true;
    int32_t gain_tenth_db_ = 297; // ~29.7 dB
    int ppm_tenths_ = 0;
    bool squelch_on_ = false;
    int  squelch_threshold_ = 30;
};

} // namespace sdr
