/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "sdr_model.h"
#include "shell_viewmodel.h"
#include "subjects.h"

#include "lvgl.h"

#include <string>

namespace sdr {

// SDR app state. Derives from the toolkit shell (dark mode, quit, NavBar page)
// and implements NavProvider to drive the 5-key bar across five tool pages
// (tuning/zoom/visual/audio/settings). Wraps SdrModel (the persisted radio
// state) and publishes the reactive subjects the SpectrumScreen observes.
class SdrViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    // The five NavBar tool pages (slot 0 / key 4 cycles between them).
    enum class Page : int {
        Tuning   = 0, // < / freq-entry / >  + fine-step toggle
        Zoom     = 1, // zoom- / band / zoom+ / mode
        Visual   = 2, // theme / freq grid / time grid / peak hold
        Audio    = 3, // mute / vol- / vol+ / vol readout
        Settings = 4, // gain- / gain+ / gain auto / exit
    };

    SdrViewModel();

    // --- SDR subjects (observed by the SpectrumScreen / header) ---
    lv_subject_t* vfo_text_subject();
    lv_subject_t* mode_text_subject();
    lv_subject_t* smeter_subject();
    lv_subject_t* band_low_text_subject();
    lv_subject_t* band_high_text_subject();
    lv_subject_t* grid_low_text_subject();
    lv_subject_t* grid_high_text_subject();
    lv_subject_t* freq_grid_subject();
    lv_subject_t* time_grid_subject();
    lv_subject_t* peak_hold_subject();
    lv_subject_t* freq_input_req_subject();
    lv_subject_t* fine_tune_subject();
    lv_subject_t* muted_subject();
    lv_subject_t* volume_subject();
    lv_subject_t* gain_text_subject();

    // --- actions (wired to NavBar slots) ---
    void tune_up();
    void tune_down();
    void set_vfo_hz(int64_t hz);     // frequency entry, clamped
    void cycle_mode();
    void toggle_fine_tune();
    void set_smeter(int level);      // 0..100, driven by the spectrum source
    void request_freq_input();       // ask the view to open the frequency dialog
    void zoom_in();
    void zoom_out();
    void cycle_band();
    void toggle_freq_grid();
    void toggle_time_grid();
    void toggle_peak_hold();
    void toggle_mute();
    void volume_up();
    void volume_down();
    void toggle_gain_auto();
    void gain_up();
    void gain_down();
    void toggle_dark();              // toggles + persists via the model

    // --- read-only state (pushed to the source / read by the screen each tick) ---
    bool    fine_tune() const;
    int32_t tune_step_hz() const;
    bool    peak_hold() const;
    bool    muted() const;
    float   volume_gain() const;     // 0..1 master gain
    bool    gain_auto() const;
    int32_t gain_tenth_db() const;
    int32_t span_khz() const;
    int32_t vfo_hz() const;
    RadioMode mode() const;
    Passband  passband() const;

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

private:
    void publish_vfo();
    void publish_mode();
    void publish_grids();
    void publish_audio();
    void publish_gain();

    SdrModel model_;

    reactive::StringSubject<16> vfo_text_subject_{""};
    reactive::StringSubject<8>  mode_text_subject_{""};
    reactive::IntSubject        smeter_subject_{0};
    reactive::StringSubject<16> band_low_text_subject_{""};
    reactive::StringSubject<16> band_high_text_subject_{""};
    reactive::StringSubject<12> grid_low_text_subject_{""};
    reactive::StringSubject<12> grid_high_text_subject_{""};
    reactive::BoolSubject       freq_grid_subject_{true};
    reactive::BoolSubject       time_grid_subject_{true};
    reactive::BoolSubject       peak_hold_subject_{false};
    reactive::IntSubject        freq_input_req_subject_{0};
    reactive::BoolSubject       fine_tune_subject_{false};
    reactive::BoolSubject       muted_subject_{false};
    reactive::IntSubject        volume_subject_{50};
    reactive::StringSubject<8>  gain_text_subject_{""};

    // Scratch buffers for the dynamic NavBar slot labels (step/gain/volume).
    // Mutable because nav_fill() is const but must point NavSlot.text at storage
    // that outlives the call (the NavBar copies the text right after).
    mutable std::string step_label_;
    mutable std::string gain_label_;
    mutable std::string vol_label_;
};

} // namespace sdr
