/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "survey_viewmodel.h"

#include "ui_const.h"

#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sys/wait.h>

extern char** environ;

namespace survey {
namespace {

// Peaks within the same 10 kHz bucket are the same transmission across sweeps.
constexpr int64_t kSeenBucketHz = 10000;

// Forget a transmission this long after it was last seen.
constexpr auto kSeenExpiry = std::chrono::seconds(120);

std::string format_mhz(int64_t hz) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(hz) / 1e6);
    return buf;
}

std::string format_age(std::chrono::seconds age) {
    char buf[16];
    if (age.count() < 60) {
        std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(age.count()));
    } else {
        std::snprintf(buf, sizeof(buf), "%lldm", static_cast<long long>(age.count() / 60));
    }
    return buf;
}

// The sibling sdr_app: same directory as this binary (device install and the
// per-app build tree both colocate the executables per app dir, so also try
// the build layout ../../sdr/<config>/sdr_app).
std::filesystem::path find_sdr_app() {
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    const auto dir = self.parent_path();
    const auto sibling = dir / "sdr_app";
    if (std::filesystem::exists(sibling, ec)) return sibling;
    // build tree: .../apps/survey/<config>/survey_app -> .../apps/sdr/<config>/sdr_app
    const auto build = dir.parent_path().parent_path() / "sdr" / dir.filename() / "sdr_app";
    if (std::filesystem::exists(build, ec)) return build;
    return {};
}

} // namespace

SurveyViewModel::SurveyViewModel() {
    set_nav_provider(this);
    set_dark_mode(model_.dark_mode());
    set_title("SURVEY");
    publish_range();
    refresh_sort_label();
}

lv_subject_t* SurveyViewModel::band_low_text_subject()  { return band_low_text_subject_.native(); }
lv_subject_t* SurveyViewModel::band_high_text_subject() { return band_high_text_subject_.native(); }
lv_subject_t* SurveyViewModel::grid_q1_text_subject()   { return grid_q1_text_subject_.native(); }
lv_subject_t* SurveyViewModel::grid_mid_text_subject()  { return grid_mid_text_subject_.native(); }
lv_subject_t* SurveyViewModel::grid_q3_text_subject()   { return grid_q3_text_subject_.native(); }
lv_subject_t* SurveyViewModel::status_text_subject()    { return status_text_subject_.native(); }
lv_subject_t* SurveyViewModel::smeter_subject()         { return smeter_subject_.native(); }
lv_subject_t* SurveyViewModel::peaks_version_subject()  { return peaks_version_subject_.native(); }
lv_subject_t* SurveyViewModel::peaks_sel_subject()      { return peaks_sel_subject_.native(); }
lv_subject_t* SurveyViewModel::sort_text_subject()      { return sort_text_subject_.native(); }
lv_subject_t* SurveyViewModel::range_input_req_subject() { return range_input_req_subject_.native(); }

void SurveyViewModel::set_smeter(int level) { smeter_subject_.set(level); }

void SurveyViewModel::update_peaks(const std::vector<SweepPeak>& peaks, bool sweep_ok) {
    const auto now = std::chrono::steady_clock::now();

    std::vector<PeakRow> rows;
    rows.reserve(peaks.size());
    for (const auto& p : peaks) {
        auto& seen = seen_[p.freq_hz / kSeenBucketHz];
        if (seen.first.time_since_epoch().count() == 0) seen.first = now;
        seen.last = now;

        const auto age =
            std::chrono::duration_cast<std::chrono::seconds>(now - seen.first);

        PeakRow row;
        row.freq_hz = p.freq_hz;
        row.dbm = p.dbm;
        row.age_s = age.count();
        row.freq = format_mhz(p.freq_hz);
        char power[16];
        std::snprintf(power, sizeof(power), "%.0f dBm", static_cast<double>(p.dbm));
        row.power = power;
        row.age = format_age(age);
        rows.push_back(std::move(row));
    }
    for (auto it = seen_.begin(); it != seen_.end();) {
        it = (now - it->second.last > kSeenExpiry) ? seen_.erase(it) : std::next(it);
    }
    sort_rows(rows);

    char status[16];
    if (!sweep_ok) {
        std::snprintf(status, sizeof(status), "sweeping...");
    } else {
        std::snprintf(status, sizeof(status), "%zu peaks", rows.size());
    }
    status_text_subject_.set(status);

    // Re-render only when the visible content actually changed.
    bool changed = rows.size() != rows_.size();
    for (size_t i = 0; !changed && i < rows.size(); ++i) {
        changed = rows[i].freq != rows_[i].freq || rows[i].power != rows_[i].power ||
                  rows[i].age != rows_[i].age;
    }
    if (changed) {
        rows_ = std::move(rows);
        clamp_selection();
        peaks_version_subject_.set(peaks_version_subject_.value() + 1);
    }
}

int SurveyViewModel::selected_peak() const { return peaks_sel_subject_.value(); }

void SurveyViewModel::clamp_selection() {
    const int last = rows_.empty() ? 0 : static_cast<int>(rows_.size()) - 1;
    if (peaks_sel_subject_.value() > last) peaks_sel_subject_.set(last);
    if (peaks_sel_subject_.value() < 0) peaks_sel_subject_.set(0);
}

void SurveyViewModel::zoom_in() {
    model_.zoom_in();
    publish_range();
}

void SurveyViewModel::zoom_out() {
    model_.zoom_out();
    publish_range();
}

void SurveyViewModel::request_range_input() {
    range_input_req_subject_.set(range_input_req_subject_.value() + 1);
}

void SurveyViewModel::set_center_span_mhz(double center_mhz, double span_mhz) {
    if (center_mhz <= 0.0 || span_mhz <= 0.0) return;
    model_.set_center_span(static_cast<int64_t>(center_mhz * 1e6 + 0.5),
                           static_cast<int64_t>(span_mhz * 1e6 + 0.5));
    publish_range();
}

void SurveyViewModel::toggle_dark() {
    model_.toggle_dark_mode();
    set_dark_mode(model_.dark_mode());
}

void SurveyViewModel::peaks_cursor_up() {
    if (peaks_sel_subject_.value() > 0) {
        peaks_sel_subject_.set(peaks_sel_subject_.value() - 1);
    }
}

void SurveyViewModel::peaks_cursor_down() {
    if (peaks_sel_subject_.value() + 1 < static_cast<int>(rows_.size())) {
        peaks_sel_subject_.set(peaks_sel_subject_.value() + 1);
    }
}

void SurveyViewModel::sort_rows(std::vector<PeakRow>& rows) const {
    const bool asc = sort_ascending_;
    std::sort(rows.begin(), rows.end(), [this, asc](const PeakRow& a, const PeakRow& b) {
        // Only the PRIMARY field honours the sort direction; ties always break
        // by frequency ascending (a swap of the whole comparator would flip the
        // tie-breaker too, which reads as arbitrary ordering to the user).
        switch (sort_field_) {
            case SortField::Power:
                if (a.dbm != b.dbm) return asc ? a.dbm < b.dbm : a.dbm > b.dbm;
                break;
            case SortField::Age:
                if (a.age_s != b.age_s) return asc ? a.age_s < b.age_s : a.age_s > b.age_s;
                break;
            case SortField::Freq:
                if (a.freq_hz != b.freq_hz) return asc ? a.freq_hz < b.freq_hz : a.freq_hz > b.freq_hz;
                break;
        }
        return a.freq_hz < b.freq_hz;
    });
}

void SurveyViewModel::refresh_sort_label() {
    const char* field = sort_field_ == SortField::Power ? "PWR"
                        : sort_field_ == SortField::Freq ? "FRQ"
                                                         : "AGE";
    const char dir = sort_ascending_ ? '^' : 'v';
    // The narrow nav slot fits only a short code (letter + direction); the
    // Peaks header shows the full field name.
    std::snprintf(sort_label_, sizeof(sort_label_), "%c%c", field[0], dir);
    char full[8];
    std::snprintf(full, sizeof(full), "%s %c", field, dir);
    sort_text_subject_.set(full);
}

void SurveyViewModel::cycle_sort() {
    // 6 states: for each field descending then ascending, then next field.
    if (!sort_ascending_) {
        sort_ascending_ = true;
    } else {
        sort_ascending_ = false;
        sort_field_ = static_cast<SortField>((static_cast<int>(sort_field_) + 1) % 3);
    }
    refresh_sort_label();

    auto rows = rows_;
    sort_rows(rows);
    rows_ = std::move(rows);
    peaks_sel_subject_.set(0);
    peaks_version_subject_.set(peaks_version_subject_.value() + 1);
    bump_nav_refresh(); // the Peaks-page sort slot shows the new label
}

void SurveyViewModel::open_selected_in_sdr() {
    const int sel = peaks_sel_subject_.value();
    if (sel < 0 || sel >= static_cast<int>(rows_.size())) return;
    const int64_t freq_hz = rows_[static_cast<size_t>(sel)].freq_hz;

    const auto sdr_app = find_sdr_app();
    if (sdr_app.empty()) {
        std::fprintf(stderr, "[survey] sdr_app not found next to this binary\n");
        return;
    }
    const std::string path = sdr_app.string();

    // Build argv + envp in the PARENT: between fork and exec the child may only
    // call async-signal-safe functions (no setenv / heap allocation). envp is
    // this process's environment plus SDR_FREQ (SDR_RTLTCP etc. pass through).
    char freq[32];
    std::snprintf(freq, sizeof(freq), "SDR_FREQ=%" PRId64, freq_hz);
    std::vector<char*> envp;
    for (char** e = environ; *e; ++e) {
        if (std::strncmp(*e, "SDR_FREQ=", 9) != 0) envp.push_back(*e);
    }
    envp.push_back(freq);
    envp.push_back(nullptr);
    char* argv[] = {const_cast<char*>(path.c_str()), nullptr};

    // Double fork so the SDR app is reparented to init (no zombie). The grandchild
    // detaches with setsid() and execve()s — both async-signal-safe.
    const pid_t pid = ::fork();
    if (pid < 0) return;
    if (pid == 0) {
        if (::fork() == 0) {
            ::setsid();
            ::execve(path.c_str(), argv, envp.data());
            _exit(127); // exec failed
        }
        _exit(0);
    }
    ::waitpid(pid, nullptr, 0);

    // Hand OFF: release the framebuffer and the RTL-SDR (our rtl_power worker)
    // so the SDR app owns the screen and the dongle, instead of both contending.
    request_quit();
}

void SurveyViewModel::publish_range() {
    const int64_t lo = model_.start_hz();
    const int64_t hi = model_.stop_hz();
    const int64_t span = hi - lo;
    band_low_text_subject_.set((format_mhz(lo) + "M").c_str());
    band_high_text_subject_.set((format_mhz(hi) + "M").c_str());
    grid_q1_text_subject_.set(format_mhz(lo + span / 4).c_str());
    grid_mid_text_subject_.set(format_mhz(lo + span / 2).c_str());
    grid_q3_text_subject_.set(format_mhz(lo + span * 3 / 4).c_str());
}


void SurveyViewModel::nav_fill(int page, NavProvider::NavSlot out[5]) const {
    out[0] = {"#", true, true}; // page number (text overridden by the NavBar)
    const bool dark = is_dark_mode();

    switch (static_cast<Page>(page)) {
        case Page::Waterfall:
            out[1] = {view::ICON_MINUS, false, true};    // zoom out (wider)
            out[2] = {view::ICON_KEYBOARD, false, true}; // tune: centre + span
            out[3] = {view::ICON_PLUS, false, true};     // zoom in (narrower)
            out[4] = {dark ? view::ICON_SUN : view::ICON_MOON, false, true};
            break;
        case Page::Peaks:
            out[1] = {view::ICON_CARET_UP, false, true};
            out[2] = {view::ICON_CARET_DOWN, false, true};
            out[3] = {view::ICON_CHECK, false, true};  // open in SDR
            out[4] = {sort_label_, true, true};        // cycle sort (ESC still quits)
            break;
    }
}

void SurveyViewModel::nav_activate(int page, int slot) {
    switch (static_cast<Page>(page)) {
        case Page::Waterfall:
            if (slot == 1) zoom_out();
            if (slot == 2) request_range_input();
            if (slot == 3) zoom_in();
            if (slot == 4) toggle_dark();
            break;
        case Page::Peaks:
            if (slot == 1) peaks_cursor_up();
            if (slot == 2) peaks_cursor_down();
            if (slot == 3) open_selected_in_sdr();
            if (slot == 4) cycle_sort();
            break;
    }
}

} // namespace survey
