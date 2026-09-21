/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "survey_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "raster.h"
#include "csv_sweep_source.h"
#include "linux_input.h"
#include "mock_sweep_source.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdlib>
#include <cstring> // std::strcmp (SURVEY_SOURCE parsing)
#include <string>

namespace survey {
namespace {

// ~10 fps: sweeps refresh at ~1 s, the scroll just provides the time axis.
constexpr uint32_t kTickPeriodMs = 100;

constexpr int32_t kChartMax = 1000;
constexpr float   kSpectrumSmooth = 0.4f;

// PEAKS table columns: FREQ | POWER | AGE (320px total).
constexpr int32_t kPeakColW[3] = {122, 110, 88};
constexpr const char* kPeakColTitle[3] = {"FREQ MHz", "POWER", "AGE"};

// Picks the sweep backend: rtl_power by default when the live backend is
// compiled in, `SURVEY_SOURCE=hackrf` for hackrf_sweep, `SURVEY_SOURCE=mock`
// for the synthetic source.
std::unique_ptr<SweepSource> make_source(const SurveyViewModel& vm) {
#ifdef SURVEY_HAVE_SWEEP
    const char* want = std::getenv("SURVEY_SOURCE");
    if (!want || std::strcmp(want, "mock") != 0) {
        const auto tool = (want && std::strcmp(want, "hackrf") == 0)
                              ? CsvSweepSource::Tool::HackrfSweep
                              : CsvSweepSource::Tool::RtlPower;
        return std::make_unique<CsvSweepSource>(tool, vm.start_hz(), vm.stop_hz(),
                                                vm.bin_hz());
    }
#endif
    auto mock = std::make_unique<MockSweepSource>();
    mock->set_range(vm.start_hz(), vm.stop_hz(), vm.bin_hz());
    return mock;
}

} // namespace

SurveyScreen::SurveyScreen(SurveyViewModel& vm, app::AssetManager& assets)
    : BaseScreen(vm, vm, assets),
      vm_(vm),
      source_(make_source(vm)),
      frame_(kBins, 0.0f),
      wf_buf_(static_cast<size_t>(kWaterfallWidth) * kWaterfallHeight, 0u) {
    init();
}

SurveyScreen::~SurveyScreen() {
    if (range_dialog_) {
        platform::set_key_capture(nullptr, nullptr);
        lv_obj_delete(range_dialog_);
        range_dialog_ = nullptr;
    }
    if (range_req_observer_) lv_observer_remove(range_req_observer_);
    if (page_observer_) lv_observer_remove(page_observer_);
    if (peaks_observer_) lv_observer_remove(peaks_observer_);
    if (peaks_sel_observer_) lv_observer_remove(peaks_sel_observer_);
    if (timer_) lv_timer_delete(timer_);
}

void SurveyScreen::range_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<SurveyScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int v = lv_subject_get_int(subject);
    if (v != self->last_range_req_) { // ignore the initial (0) notification
        self->last_range_req_ = v;
        self->open_range_dialog();
    }
}

void SurveyScreen::build_content(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 0, 0);

    build_waterfall_view(content);
    build_peaks_view(content);

    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);

    // The NavBar tool page picks the visible view (fires with the current value).
    page_observer_ =
        lv_subject_add_observer(vm_.toolbar_page_subject(), page_cb, this);
    peaks_observer_ =
        lv_subject_add_observer(vm_.peaks_version_subject(), peaks_cb, this);
    peaks_sel_observer_ =
        lv_subject_add_observer(vm_.peaks_sel_subject(), peaks_cb, this);
    range_req_observer_ =
        lv_subject_add_observer(vm_.range_input_req_subject(), range_req_cb, this);
}

void SurveyScreen::build_waterfall_view(lv_obj_t* content) {
    waterfall_view_ = lv_obj_create(content);
    lv_obj_remove_style_all(waterfall_view_);
    lv_obj_set_size(waterfall_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(waterfall_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(waterfall_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(waterfall_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(waterfall_view_, 0, 0);
    lv_obj_set_style_pad_row(waterfall_view_, kRowPad, 0);

    auto* small_font = assets().load_font("inter-regular.ttf", 12);
    auto* mono_font  = assets().load_font("inter-semibold.ttf", 12);

    // --- Header row: status | S-meter ---
    auto* header = lv_obj_create(waterfall_view_);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kHeaderHeight);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    auto* status = lv_label_create(header);
    lv_label_bind_text(status, vm_.status_text_subject(), nullptr);
    lv_obj_set_style_text_font(status, mono_font ? mono_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(status, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(status, LV_ALIGN_LEFT_MID, 4, 0);

    auto* smeter_box = lv_obj_create(header);
    lv_obj_remove_style_all(smeter_box);
    lv_obj_set_size(smeter_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(smeter_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(smeter_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(smeter_box, 3, 0);
    lv_obj_align(smeter_box, LV_ALIGN_RIGHT_MID, -4, 0);

    battery_ = std::make_unique<view::widgets::BatteryBadge>(smeter_box);
    lv_obj_set_style_margin_right(battery_->obj(), 6, 0);

    auto* s_label = lv_label_create(smeter_box);
    lv_label_set_text(s_label, "S");
    lv_obj_set_style_text_font(s_label, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(s_label, vm_.dark_mode_subject(), reactive::ThemeRole::Text);

    auto* smeter = lv_bar_create(smeter_box);
    lv_obj_set_size(smeter, 60, 8);
    lv_bar_set_range(smeter, 0, 100);
    lv_obj_set_style_bg_color(smeter, view::palette(false).primary, LV_PART_INDICATOR);
    lv_obj_remove_flag(smeter, LV_OBJ_FLAG_CLICKABLE);
    reactive::bind_bar_value(smeter, vm_.smeter_subject());

    // --- Spectrum line chart ---
    chart_ = lv_chart_create(waterfall_view_);
    lv_obj_set_size(chart_, LV_PCT(100), kChartHeight);
    lv_obj_set_style_pad_all(chart_, 0, 0);
    lv_obj_set_style_border_width(chart_, 0, 0);
    lv_obj_set_style_bg_opa(chart_, LV_OPA_TRANSP, 0);
    lv_chart_set_div_line_count(chart_, 0, 0);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_, kBins);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, kChartMax);
    lv_obj_set_style_size(chart_, 0, 0, LV_PART_INDICATOR);
    // Hairline traces: the theme default (2-3 px, rounded caps) smears the peaks.
    lv_obj_set_style_line_width(chart_, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart_, false, LV_PART_ITEMS);
    lv_obj_remove_flag(chart_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(chart_, LV_OBJ_FLAG_CLICKABLE);
    series_ = lv_chart_add_series(chart_, view::palette(false).primary,
                                  LV_CHART_AXIS_PRIMARY_Y);
    smooth_.assign(kBins, 0.0f);

    // --- Peak markers: a pool of small dots over the chart, one per detected
    // transmission, repositioned to the peak's frequency column each sweep. ---
    peak_markers_.reserve(kMaxPeakMarkers);
    for (int i = 0; i < kMaxPeakMarkers; ++i) {
        auto* dot = lv_obj_create(chart_);
        lv_obj_remove_style_all(dot);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(dot, 5, 5);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xffcc00), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_border_color(dot, lv_color_black(), 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        peak_markers_.push_back(dot);
    }

    // --- Waterfall canvas (RGB565, full width) ---
    waterfall_ = lv_canvas_create(waterfall_view_);
    lv_canvas_set_buffer(waterfall_, wf_buf_.data(), kWaterfallWidth, kWaterfallHeight,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(waterfall_, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_size(waterfall_, kWaterfallWidth, kWaterfallHeight);
    lv_obj_remove_flag(waterfall_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(waterfall_, LV_OBJ_FLAG_CLICKABLE);

    // --- Vertical reference lines at 1/4, 1/2, 3/4 of the span, aligned with
    // the frequency labels below, so a peak's frequency is readable off the grid.
    for (int k = 1; k < 4; ++k) {
        auto* line = lv_obj_create(waterfall_);
        lv_obj_remove_style_all(line);
        lv_obj_set_size(line, 1, kWaterfallHeight);
        lv_obj_align(line, LV_ALIGN_TOP_LEFT, k * kWaterfallWidth / 4, 0);
        lv_obj_set_style_bg_color(line, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }

    // --- Sweep-range labels overlaid on the waterfall corners ---
    auto* tiny_font = assets().load_font("inter-regular.ttf", 10);
    const auto add_band_label = [&](lv_subject_t* subject, lv_align_t align) {
        auto* lbl = lv_label_create(waterfall_);
        lv_label_bind_text(lbl, subject, nullptr);
        lv_obj_set_style_text_font(lbl, tiny_font ? tiny_font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_bg_color(lbl, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_40, 0);
        lv_obj_set_style_pad_hor(lbl, 2, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(lbl, align, 0, -(kNavBandHeight + 2));
    };
    add_band_label(vm_.band_low_text_subject(),  LV_ALIGN_BOTTOM_LEFT);
    add_band_label(vm_.band_high_text_subject(), LV_ALIGN_BOTTOM_RIGHT);

    // Intermediate frequency ticks (quarter / centre / three-quarter), like the
    // SDR view's grid labels, so the swept span is readable at a glance.
    const auto add_grid_label = [&](lv_subject_t* subject, int32_t x_off) {
        auto* lbl = lv_label_create(waterfall_);
        lv_label_bind_text(lbl, subject, nullptr);
        lv_obj_set_style_text_font(lbl, tiny_font ? tiny_font : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_bg_color(lbl, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_40, 0);
        lv_obj_set_style_pad_hor(lbl, 2, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, x_off, -(kNavBandHeight + 2));
    };
    add_grid_label(vm_.grid_q1_text_subject(),  -kWaterfallWidth / 4);
    add_grid_label(vm_.grid_mid_text_subject(),  0);
    add_grid_label(vm_.grid_q3_text_subject(),   kWaterfallWidth / 4);
}

void SurveyScreen::build_peaks_view(lv_obj_t* content) {
    auto* fs = assets().load_font("inter-regular.ttf", 12);

    peaks_view_ = lv_obj_create(content);
    lv_obj_remove_style_all(peaks_view_);
    lv_obj_add_flag(peaks_view_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(peaks_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(peaks_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(peaks_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(peaks_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(peaks_view_, 0, 0);
    lv_obj_set_style_pad_row(peaks_view_, 0, 0);
    reactive::bind_theme(peaks_view_, vm_.dark_mode_subject(), reactive::ThemeRole::Surface);
    lv_obj_add_flag(peaks_view_, LV_OBJ_FLAG_HIDDEN);

    auto* header = lv_obj_create(peaks_view_);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kHeaderHeight);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    int32_t hx = 0;
    for (int c = 0; c < 3; ++c) {
        auto* h = lv_label_create(header);
        lv_label_set_text(h, kPeakColTitle[c]);
        lv_obj_set_style_text_font(h, fs ? fs : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0x888888), 0);
        lv_obj_align(h, LV_ALIGN_LEFT_MID, hx + 4, 0);
        hx += kPeakColW[c];
    }

    // Active sort indicator (e.g. "PWR v"), right-aligned in the header. The
    // selected row is marked in the table itself (accent band + ">" prefix).
    auto* sort_lbl = lv_label_create(header);
    lv_label_bind_text(sort_lbl, vm_.sort_text_subject(), nullptr);
    lv_obj_set_style_text_font(sort_lbl, fs ? fs : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sort_lbl, view::palette(false).primary, 0);
    lv_obj_align(sort_lbl, LV_ALIGN_RIGHT_MID, -4, 0);

    peaks_table_ = lv_table_create(peaks_view_);
    lv_obj_set_width(peaks_table_, LV_PCT(100));
    lv_obj_set_flex_grow(peaks_table_, 1);
    lv_table_set_column_count(peaks_table_, 3);
    for (int c = 0; c < 3; ++c) lv_table_set_column_width(peaks_table_, c, kPeakColW[c]);
    lv_obj_set_style_pad_all(peaks_table_, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_width(peaks_table_, 0, 0);
    lv_obj_set_style_text_font(peaks_table_, fs ? fs : &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_remove_flag(peaks_table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(peaks_table_, peaks_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(peaks_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
}

void SurveyScreen::page_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<SurveyScreen*>(lv_observer_get_user_data(observer));
    if (self) self->apply_page(lv_subject_get_int(subject));
}

void SurveyScreen::apply_page(int page) {
    const bool peaks = page == static_cast<int>(SurveyViewModel::Page::Peaks);
    if (waterfall_view_) {
        if (peaks) lv_obj_add_flag(waterfall_view_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(waterfall_view_, LV_OBJ_FLAG_HIDDEN);
    }
    if (peaks_view_) {
        if (peaks) lv_obj_remove_flag(peaks_view_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(peaks_view_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SurveyScreen::peaks_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<SurveyScreen*>(lv_observer_get_user_data(observer));
    if (self) self->update_peaks_table();
}

void SurveyScreen::peaks_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<SurveyScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) return;
    const bool is_sel = static_cast<int>(base->id1) == self->vm_.selected_peak();
    const lv_draw_task_type_t type = lv_draw_task_get_type(task);

    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel && base->part == LV_PART_ITEMS) {
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(self->vm_.is_dark_mode()).primary;
        fd->opa = LV_OPA_COVER;
        return;
    }
    if (type == LV_DRAW_TASK_TYPE_LABEL && is_sel) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        ld->color = lv_color_black(); // contrast against the accent band
    }
}

void SurveyScreen::update_peaks_table() {
    if (!peaks_table_) return;
    const auto& rows = vm_.peak_rows();
    lv_table_set_row_count(peaks_table_, static_cast<uint32_t>(rows.size()));
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto r = static_cast<uint32_t>(i);
        lv_table_set_cell_value(peaks_table_, r, 0, rows[i].freq.c_str());
        lv_table_set_cell_value(peaks_table_, r, 1, rows[i].power.c_str());
        lv_table_set_cell_value(peaks_table_, r, 2, rows[i].age.c_str());
    }
    // Keep the cursor on screen: set_selected_cell scrolls the table to the row
    // (same as the ADS-B/AIS/ISM lists).
    const int sel = vm_.selected_peak();
    if (sel >= 0 && sel < static_cast<int>(rows.size())) {
        lv_table_set_selected_cell(peaks_table_, static_cast<uint16_t>(sel), 0);
    }
    lv_obj_invalidate(peaks_table_);
}

void SurveyScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<SurveyScreen*>(lv_timer_get_user_data(timer));
    if (self) self->tick();
}

void SurveyScreen::tick() {
    if (!source_) return;

    // Keep the backend on the model's range (no-op when unchanged).
    source_->set_range(vm_.start_hz(), vm_.stop_hz(), vm_.bin_hz());

    source_->next_frame(frame_.data(), kBins);

    if (chart_ && series_) {
        int32_t* y = lv_chart_get_series_y_array(chart_, series_);
        if (y) {
            for (int i = 0; i < kBins; ++i) {
                smooth_[i] += kSpectrumSmooth * (frame_[i] - smooth_[i]); // EMA
                y[i] = static_cast<int32_t>(smooth_[i] * static_cast<float>(kChartMax));
            }
        }
        lv_chart_refresh(chart_);
    }

    push_waterfall_row(frame_.data());

    const auto peaks = source_->peaks();
    update_peak_markers(peaks);
    vm_.set_smeter(static_cast<int>(source_->last_peak() * 100.0f));
    vm_.update_peaks(peaks, source_->ok());
}

void SurveyScreen::update_peak_markers(const std::vector<SweepPeak>& peaks) {
    if (peak_markers_.empty()) return;
    const int64_t start = vm_.start_hz();
    const int64_t stop = vm_.stop_hz();
    const int64_t span = stop - start;
    if (span <= 0) return;

    // The selected list row carries the open-in-SDR target frequency; its dot is
    // drawn larger and in the accent colour so it stands out among the peaks.
    const auto& rows = vm_.peak_rows();
    const int sel = vm_.selected_peak();
    const int64_t sel_hz =
        (sel >= 0 && sel < static_cast<int>(rows.size())) ? rows[static_cast<size_t>(sel)].freq_hz : -1;
    const lv_color_t accent = view::palette(vm_.is_dark_mode()).primary;

    size_t shown = 0;
    for (const auto& p : peaks) {
        if (shown >= peak_markers_.size()) break;
        if (p.freq_hz < start || p.freq_hz >= stop) continue;
        const int32_t x = static_cast<int32_t>((p.freq_hz - start) * kWaterfallWidth / span);
        auto* dot = peak_markers_[shown++];
        const bool is_sel = p.freq_hz == sel_hz;
        const int32_t sz = is_sel ? 8 : 5;
        lv_obj_set_size(dot, sz, sz);
        lv_obj_set_style_bg_color(dot, is_sel ? accent : lv_color_hex(0xffcc00), 0);
        lv_obj_align(dot, LV_ALIGN_TOP_LEFT, x - sz / 2, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t i = shown; i < peak_markers_.size(); ++i) {
        lv_obj_add_flag(peak_markers_[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void SurveyScreen::push_waterfall_row(const float* mags) {
    if (!waterfall_ || wf_buf_.empty()) return;

    view::push_waterfall_row(wf_buf_.data(), kWaterfallWidth, kWaterfallHeight, mags);

    lv_obj_invalidate(waterfall_);
}

void SurveyScreen::range_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<SurveyScreen*>(ctx)) self->on_range_key(key);
}

void SurveyScreen::open_range_dialog() {
    if (range_dialog_) return;
    // Seed the fields from the current range so tweaking is easy.
    const double center = (vm_.start_hz() + vm_.stop_hz()) / 2.0 / 1e6;
    const double span = (vm_.stop_hz() - vm_.start_hz()) / 1e6;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.3f", center);
    range_center_buf_ = buf;
    std::snprintf(buf, sizeof(buf), "%.3f", span);
    range_span_buf_ = buf;
    range_editing_span_ = false;

    auto* value_font = assets().load_font("inter-semibold.ttf", 20);
    auto* small_font = assets().load_font("inter-regular.ttf", 12);
    const auto* fb = &lv_font_montserrat_12;

    range_dialog_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(range_dialog_);
    lv_obj_set_size(range_dialog_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(range_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(range_dialog_, LV_OPA_80, 0);
    lv_obj_clear_flag(range_dialog_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(range_dialog_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(range_dialog_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(range_dialog_, 3, 0);

    auto* title = lv_label_create(range_dialog_);
    lv_label_set_text(title, "Tune (MHz)");
    lv_obj_set_style_text_font(title, small_font ? small_font : fb, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    range_center_label_ = lv_label_create(range_dialog_);
    lv_obj_set_style_text_font(range_center_label_, value_font ? value_font : fb, 0);
    range_span_label_ = lv_label_create(range_dialog_);
    lv_obj_set_style_text_font(range_span_label_, value_font ? value_font : fb, 0);

    auto* hint = lv_label_create(range_dialog_);
    lv_label_set_text(hint, "Enter = next/OK   Esc = cancel");
    lv_obj_set_style_text_font(hint, small_font ? small_font : fb, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xaaaaaa), 0);

    update_range_labels();
    platform::set_key_capture(range_key_cb, this);
}

void SurveyScreen::update_range_labels() {
    const lv_color_t active = view::palette(vm_.is_dark_mode()).primary;
    const lv_color_t idle = lv_color_hex(0x888888);
    if (range_center_label_) {
        lv_label_set_text_fmt(range_center_label_, "C %s",
                              range_center_buf_.empty() ? "_" : range_center_buf_.c_str());
        lv_obj_set_style_text_color(range_center_label_,
                                    range_editing_span_ ? idle : active, 0);
    }
    if (range_span_label_) {
        lv_label_set_text_fmt(range_span_label_, "W %s",
                              range_span_buf_.empty() ? "_" : range_span_buf_.c_str());
        lv_obj_set_style_text_color(range_span_label_,
                                    range_editing_span_ ? active : idle, 0);
    }
}

void SurveyScreen::on_range_key(uint32_t key) {
    std::string& buf = range_editing_span_ ? range_span_buf_ : range_center_buf_;
    if (key >= '0' && key <= '9') {
        const auto dot = buf.find('.');
        const bool frac_full = dot != std::string::npos && (buf.size() - dot - 1) >= 3;
        if (!frac_full && buf.size() < 9) {
            buf.push_back(static_cast<char>(key));
            update_range_labels();
        }
    } else if (key == '.') {
        if (buf.find('.') == std::string::npos) {
            if (buf.empty()) buf = "0";
            buf.push_back('.');
            update_range_labels();
        }
    } else if (key == LV_KEY_BACKSPACE) {
        if (!buf.empty()) {
            buf.pop_back();
            update_range_labels();
        }
    } else if (key == LV_KEY_ENTER) {
        if (!range_editing_span_) {
            range_editing_span_ = true; // centre done -> edit span
            update_range_labels();
        } else {
            close_range_dialog(true);
        }
    } else if (key == LV_KEY_ESC) {
        close_range_dialog(false);
    }
}

void SurveyScreen::close_range_dialog(bool apply) {
    platform::set_key_capture(nullptr, nullptr);
    if (apply) {
        const double center = std::strtod(range_center_buf_.c_str(), nullptr);
        const double span = std::strtod(range_span_buf_.c_str(), nullptr);
        vm_.set_center_span_mhz(center, span);
    }
    if (range_dialog_) {
        lv_obj_delete(range_dialog_);
        range_dialog_ = nullptr;
    }
    range_center_label_ = nullptr;
    range_span_label_ = nullptr;
}

} // namespace survey
