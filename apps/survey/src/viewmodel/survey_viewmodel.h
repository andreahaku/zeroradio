/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "shell_viewmodel.h"
#include "subjects.h"
#include "survey_model.h"
#include "sweep_accumulator.h"

#include "lvgl.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace survey {

// Survey app state: the shell (dark mode, quit, NavBar pages) + NavProvider
// over two tool pages (waterfall / peaks), the persisted SurveyModel, and the
// live PEAKS list (current sweep peaks + how long each has been on the air).
class SurveyViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    // NavBar tool pages; the screen shows the matching view per page.
    enum class Page : int {
        Waterfall = 0, // zoom- / range preset / zoom+ / theme
        Peaks     = 1, // cursor up / cursor down / open in SDR / exit
    };

    SurveyViewModel();

    // One formatted PEAKS row (FREQ | POWER | AGE). Numeric fields drive the sort;
    // the strings are what the table shows.
    struct PeakRow {
        int64_t freq_hz;
        float   dbm;
        int64_t age_s;
        std::string freq;
        std::string power;
        std::string age;
    };

    // PEAKS list sort: one of three columns, ascending or descending. Cycled by
    // the Peaks-page sort key; default = POWER descending (strongest first).
    enum class SortField : int { Power = 0, Freq = 1, Age = 2 };

    // --- subjects (observed by SurveyScreen) ---
    lv_subject_t* band_low_text_subject();
    lv_subject_t* band_high_text_subject();
    lv_subject_t* grid_q1_text_subject();     // frequency at 1/4 of the span
    lv_subject_t* grid_mid_text_subject();    // frequency at the centre
    lv_subject_t* grid_q3_text_subject();     // frequency at 3/4 of the span
    lv_subject_t* status_text_subject();   // "sweeping..." / "N peaks"
    lv_subject_t* smeter_subject();
    lv_subject_t* peaks_version_subject(); // bumped when the rows change
    lv_subject_t* peaks_sel_subject();
    lv_subject_t* sort_text_subject();     // current sort, e.g. "PWR v"
    lv_subject_t* range_input_req_subject(); // bumped to open the tune dialog

    // --- swept range (pushed to the sweep source each tick) ---
    int64_t start_hz() const { return model_.start_hz(); }
    int64_t stop_hz() const { return model_.stop_hz(); }
    int32_t bin_hz() const { return model_.bin_hz(); }

    // --- live updates from the screen tick ---
    void set_smeter(int level); // 0..100
    void update_peaks(const std::vector<SweepPeak>& peaks, bool sweep_ok);

    // --- PEAKS table state ---
    const std::vector<PeakRow>& peak_rows() const { return rows_; }
    int selected_peak() const;

    // --- actions ---
    void zoom_in();
    void zoom_out();
    void request_range_input();  // bumps range_input_req -> screen opens the dialog
    void set_center_span_mhz(double center_mhz, double span_mhz); // manual tune
    void toggle_dark(); // toggles + persists via the model
    void peaks_cursor_up();
    void peaks_cursor_down();
    void cycle_sort();           // advance sort field/direction, re-sort the list
    void open_selected_in_sdr(); // hand the selected peak off to the sibling sdr_app

    // Pending "Open in SDR" hand-off, set by open_selected_in_sdr() before it
    // requests quit. main() execs it once run_app has released the display and
    // the dongle (empty path = plain exit).
    const std::string& handoff_sdr_path() const { return handoff_sdr_path_; }
    int64_t handoff_freq_hz() const { return handoff_freq_hz_; }

    // Short label of the active sort for the Peaks-page nav slot (e.g. "PWR v").
    const char* sort_label() const { return sort_label_; }

    // --- NavProvider ---
    int nav_page_count() const override { return 2; }
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;

private:
    void publish_range();
    void clamp_selection();
    void sort_rows(std::vector<PeakRow>& rows) const;
    void refresh_sort_label();

    SurveyModel model_;

    reactive::StringSubject<16> band_low_text_subject_{""};
    reactive::StringSubject<16> band_high_text_subject_{""};
    reactive::StringSubject<16> grid_q1_text_subject_{""};
    reactive::StringSubject<16> grid_mid_text_subject_{""};
    reactive::StringSubject<16> grid_q3_text_subject_{""};
    reactive::StringSubject<16> status_text_subject_{"sweeping..."};
    reactive::IntSubject        smeter_subject_{0};
    reactive::IntSubject        peaks_version_subject_{0};
    reactive::IntSubject        peaks_sel_subject_{0};
    reactive::IntSubject        range_input_req_subject_{0};
    reactive::StringSubject<8>  sort_text_subject_{"PWR v"};

    SortField sort_field_ = SortField::Power;
    bool      sort_ascending_ = false; // default: strongest (power desc) first
    char      sort_label_[8] = "PWR v";

    std::vector<PeakRow> rows_;

    std::string handoff_sdr_path_;
    int64_t     handoff_freq_hz_ = 0;

    // How long each transmission has been on the air: first/last-seen wall
    // times keyed by 10 kHz frequency bucket (stable across sweep jitter).
    struct Seen {
        std::chrono::steady_clock::time_point first;
        std::chrono::steady_clock::time_point last;
    };
    std::map<int64_t, Seen> seen_;
};

} // namespace survey
