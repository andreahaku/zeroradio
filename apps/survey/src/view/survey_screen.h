/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "battery_badge.h"
#include "survey_viewmodel.h"
#include "sweep_source.h"

#include "lvgl.h"

#include <memory>
#include <string>
#include <vector>

namespace survey {

// Survey view: page 0 is a wide-band waterfall (header + spectrum chart +
// scrolling RGB565 canvas, the SDR app's pipeline over a sweep source);
// page 1 is the PEAKS table (FREQ | POWER | AGE) with a cursor band and the
// open-in-SDR action. The NavBar tool page picks the visible view.
class SurveyScreen : public screen::BaseScreen {
public:
    SurveyScreen(SurveyViewModel& vm, app::AssetManager& assets);
    ~SurveyScreen() override;

protected:
    void build_content(lv_obj_t* content) override;
    bool show_title_bar() const override { return false; }
    bool overlay_nav_bar() const override { return true; }

private:
    static constexpr int32_t kHeaderHeight    = 18;
    static constexpr int32_t kChartHeight     = 30;
    static constexpr int32_t kRowPad          = 1;
    static constexpr int32_t kNavBandHeight   = 30;
    static constexpr int32_t kScreenH         = 170;
    static constexpr int32_t kWaterfallWidth  = 320;
    static constexpr int32_t kWaterfallHeight =
        kScreenH - kHeaderHeight - kRowPad - kChartHeight - kRowPad;
    static constexpr int     kBins            = kWaterfallWidth;

    // Up to this many peak markers are drawn over the spectrum chart at once.
    static constexpr int kMaxPeakMarkers = 24;

    static void tick_cb(lv_timer_t* timer);
    void tick();
    void push_waterfall_row(const float* mags);
    // Place a dot over the spectrum chart at each detected peak's frequency.
    void update_peak_markers(const std::vector<SweepPeak>& peaks);

    void build_waterfall_view(lv_obj_t* content);
    void build_peaks_view(lv_obj_t* content);

    static void page_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void peaks_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void peaks_draw_event_cb(lv_event_t* event);
    void apply_page(int page);
    void update_peaks_table();

    // Manual range dialog: type Centre then Span (MHz), Enter advances/commits,
    // Esc cancels. Mirrors the SDR app's frequency-entry modal (raw key capture).
    static void range_req_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void range_key_cb(uint32_t key, void* ctx);
    void open_range_dialog();
    void close_range_dialog(bool apply);
    void on_range_key(uint32_t key);
    void update_range_labels();

    SurveyViewModel& vm_;
    std::unique_ptr<SweepSource> source_;
    std::vector<float>    frame_;
    std::vector<uint16_t> wf_buf_;

    lv_obj_t*          waterfall_view_ = nullptr;
    lv_obj_t*          peaks_view_     = nullptr;
    lv_obj_t*          chart_          = nullptr;
    std::unique_ptr<view::widgets::BatteryBadge> battery_; // header, left of the S-meter
    lv_chart_series_t* series_         = nullptr;
    std::vector<float> smooth_;
    lv_obj_t*          waterfall_      = nullptr;
    lv_obj_t*          peaks_table_    = nullptr;
    std::vector<lv_obj_t*> peak_markers_; // pooled dots over the spectrum chart

    lv_observer_t* page_observer_      = nullptr;
    lv_observer_t* peaks_observer_     = nullptr;
    lv_observer_t* peaks_sel_observer_ = nullptr;
    lv_timer_t*    timer_              = nullptr;

    // Range dialog state.
    lv_obj_t*   range_dialog_       = nullptr;
    lv_obj_t*   range_center_label_ = nullptr;
    lv_obj_t*   range_span_label_   = nullptr;
    std::string range_center_buf_;
    std::string range_span_buf_;
    bool        range_editing_span_ = false; // false = editing centre
    lv_observer_t* range_req_observer_ = nullptr;
    int         last_range_req_     = 0;
};

} // namespace survey
