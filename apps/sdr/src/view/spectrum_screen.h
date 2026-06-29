/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "sdr_viewmodel.h"
#include "spectrum_source.h"

#include "lvgl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sdr {

// Live SDR view: header (VFO / mode / S-meter), an FFT magnitude line chart, and
// a scrolling RGB565 waterfall canvas, all fed by a SpectrumSource on a timer.
// Ported from SDRTerminal onto the toolkit's decoupled BaseScreen; the layout
// already fits 320x170 (18 + 1 + 30 + 1 + 120).
class SpectrumScreen : public screen::BaseScreen {
public:
    SpectrumScreen(SdrViewModel& vm, app::AssetManager& assets);
    ~SpectrumScreen() override;

protected:
    void build_content(lv_obj_t* content) override;
    // No TitleBar on the SDR view: the reclaimed 30px go to the waterfall.
    bool show_title_bar() const override { return false; }
    // Translucent NavBar: the waterfall continues full-height behind it.
    bool overlay_nav_bar() const override { return true; }

private:
    static void tick_cb(lv_timer_t* timer);
    void tick();
    void push_waterfall_row(const float* mags);

    // Vertical layout of the content column (used to size the centre marker).
    static constexpr int32_t kHeaderHeight    = 18;
    static constexpr int32_t kChartHeight     = 30;
    static constexpr int32_t kRowPad          = 1;  // pad_row between content rows

    // Waterfall canvas geometry (full 320px width). Height fills the content
    // down to the screen bottom; the overlay NavBar floats over its lower band.
    static constexpr int32_t kNavBandHeight   = 30; // matches view::kNavBarHeight
    static constexpr int32_t kWaterfallWidth  = 320;
    static constexpr int32_t kWaterfallHeight = 120;
    static constexpr int     kBins            = kWaterfallWidth; // 1 bin per column

    // The waterfall buffer is allocated at the FULL chart+waterfall area height so
    // the canvas object can grow to cover the spectrum chart (the "100% waterfall"
    // split) WITHOUT ever calling lv_canvas_set_buffer again (a runtime resize of
    // the canvas buffer crashes the flush — see the project memory). Only the
    // currently visible rows (`wf_rows_`) are scrolled/colormapped per frame, so a
    // smaller split costs less, not more.
    // Full chart+waterfall area below the header, down to the screen bottom (the
    // overlay NavBar floats over its lower band). The split divides THIS area, so
    // the waterfall always reaches the bottom edge (no black gap under the navbar).
    static constexpr int32_t kScreenH            = 170; // view::kScreenHeight (device panel)
    static constexpr int32_t kSplitArea          = kScreenH - kHeaderHeight;
    static constexpr int32_t kMaxWaterfallHeight  = kSplitArea; // canvas buffer rows

    void build_grids(lv_obj_t* parent);
    void update_passband();
    void update_peak();

    // Waterfall/spectrum split (page-3 key 7): resize the canvas object and the
    // spectrum chart to `rows` waterfall rows (0..kMaxWaterfallHeight) without
    // touching the canvas buffer. Newly revealed rows are cleared.
    void apply_wf_split(int32_t rows);
    static void wf_split_cb(lv_observer_t* observer, lv_subject_t* subject);

    // Modal frequency-entry dialog (manual key capture, no LVGL focus group).
    static void freq_req_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void freq_key_cb(uint32_t key, void* ctx);
    void open_freq_dialog();
    void close_freq_dialog(bool apply);
    void on_freq_key(uint32_t key);
    void update_freq_label();

    SdrViewModel& vm_;
    std::unique_ptr<SpectrumSource> source_;
    std::vector<float>    frame_;   // kBins magnitudes, reused each tick
    std::vector<uint16_t> wf_buf_;  // RGB565 canvas backing store (W*H)

    lv_obj_t*           chart_       = nullptr;
    lv_chart_series_t*  series_      = nullptr;
    lv_chart_series_t*  peak_series_ = nullptr; // decaying peak-hold trace
    std::vector<float>  peak_;                  // per-bin held peak [0,1]
    bool                peak_visible_ = false;
    lv_obj_t*           waterfall_  = nullptr;
    lv_obj_t*           freq_grid_  = nullptr; // vertical lines (frequency)
    int                 marker_count_ = 0;     // ticks since the last 1 s time marker
    int32_t             wf_rows_      = kWaterfallHeight; // visible waterfall rows
    lv_observer_t*      wf_split_observer_ = nullptr;
    lv_obj_t*           passband_   = nullptr; // demod bandwidth highlight
    int                 last_pb_x_  = -1;
    int                 last_pb_w_  = -1;
    lv_timer_t*         timer_      = nullptr;

    lv_obj_t*           freq_dialog_      = nullptr;
    lv_obj_t*           freq_value_label_ = nullptr;
    lv_observer_t*      freq_req_observer_ = nullptr;
    int                 last_freq_req_    = 0;
    std::string         freq_buf_;
};

} // namespace sdr
