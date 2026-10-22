/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "spectrum_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "raster.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#ifdef SDR_HAVE_RTLTCP
#include "rtl_tcp_source.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring> // std::strcmp (SDR_SOURCE parsing)
#include <memory>
#include <string>

namespace sdr {
namespace {

// Refresh period in ms. ~33 ms ≈ 30 fps for the waterfall scroll.
constexpr uint32_t kTickPeriodMs = 33;

// Waterfall rows per ~1 s, used to stamp the scrolling time-marker line.
constexpr int kMarkerPeriodTicks = 1000 / kTickPeriodMs;

// Chart Y range. Magnitudes are [0,1]; scale to 0..1000 for integer chart data.
constexpr int32_t kChartMax = 1000;

// Peak-hold decay per tick (~33 ms): peaks fade over ~3-4 s ("temporary hold").
constexpr float kPeakDecay = 0.99f;

// EMA smoothing for the live (green) spectrum line: each tick moves the
// displayed value a fraction toward the new magnitude, taming the per-frame
// jitter. Lower = smoother/slower. Peak-hold still tracks the raw magnitudes.
constexpr float kSpectrumSmooth = 0.4f;

// 80% blend of an RGB565 pixel toward black for the 1 s time-marker line.
inline uint16_t blend_black(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5) & 0x3F;
    uint16_t b = c & 0x1F;
    r = static_cast<uint16_t>((r * 51) >> 8); // *0.2
    g = static_cast<uint16_t>((g * 51) >> 8);
    b = static_cast<uint16_t>((b * 51) >> 8);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Default VFO centre (Hz) the real source tunes to before the first tick.
constexpr int64_t kDefaultCenterHz = 145500000;

// Picks the spectrum backend. Defaults to the live RTL-SDR (via rtl_tcp) when
// built with SDR_HAVE_RTLTCP; `SDR_SOURCE=mock` forces the synthetic source.
// By default it starts a local rtl_tcp on 127.0.0.1:1234 for the attached dongle;
// `SDR_RTLTCP=host:port` instead connects to an existing (e.g. remote) server.
std::unique_ptr<SpectrumSource> make_source() {
#ifdef SDR_HAVE_RTLTCP
    const char* want = std::getenv("SDR_SOURCE");
    if (!want || std::strcmp(want, "mock") != 0) {
        std::string host = "127.0.0.1";
        uint16_t    port = 1234;
        // No SDR_RTLTCP: the dongle is on this device, so the source runs its own
        // local rtl_tcp. With SDR_RTLTCP we only connect to the given server.
        bool spawn_local = true;
        if (const char* ep = std::getenv("SDR_RTLTCP"); ep && ep[0] != '\0') {
            spawn_local = false;
            const std::string s = ep;
            const auto colon = s.find(':');
            if (colon != std::string::npos) {
                host = s.substr(0, colon);
                port = static_cast<uint16_t>(std::atoi(s.c_str() + colon + 1));
            } else {
                host = s;
            }
        }
        return std::make_unique<RtlTcpSource>(host, port, kDefaultCenterHz, spawn_local);
    }
#endif
    return std::make_unique<MockSpectrumSource>();
}

} // namespace

SpectrumScreen::SpectrumScreen(SdrViewModel& vm, app::AssetManager& assets)
    : BaseScreen(vm, vm, assets),
      vm_(vm),
      source_(make_source()),
      frame_(kBins, 0.0f),
      wf_buf_(static_cast<size_t>(kWaterfallWidth) * kMaxWaterfallHeight, 0u) {
    init();
}

SpectrumScreen::~SpectrumScreen() {
    if (freq_dialog_) {
        platform::set_key_capture(nullptr, nullptr);
        lv_obj_delete(freq_dialog_);
        freq_dialog_ = nullptr;
    }
    if (freq_req_observer_) {
        lv_observer_remove(freq_req_observer_);
        freq_req_observer_ = nullptr;
    }
    if (ppm_req_observer_) {
        lv_observer_remove(ppm_req_observer_);
        ppm_req_observer_ = nullptr;
    }
    if (memory_req_observer_) {
        lv_observer_remove(memory_req_observer_);
        memory_req_observer_ = nullptr;
    }
    memory_dialog_.reset();
    if (toolbar_page_observer_) {
        lv_observer_remove(toolbar_page_observer_);
        toolbar_page_observer_ = nullptr;
    }
    if (status_text_observer_) {
        lv_observer_remove(status_text_observer_);
        status_text_observer_ = nullptr;
    }
    if (wf_split_observer_) {
        lv_observer_remove(wf_split_observer_);
        wf_split_observer_ = nullptr;
    }
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void SpectrumScreen::build_content(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 1, 0);

    auto* small_font = assets().load_font("inter-regular.ttf", 12);
    auto* mono_font  = assets().load_font("inter-semibold.ttf", 12);

    // --- Header row: VFO | mode | S-meter ---
    auto* header = lv_obj_create(content);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kHeaderHeight);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    auto* mode = lv_label_create(header);
    lv_label_bind_text(mode, vm_.mode_text_subject(), nullptr);
    lv_obj_set_style_text_font(mode, mono_font ? mono_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(mode, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(mode, LV_ALIGN_LEFT_MID, 4, 0);

    auto* vfo = lv_label_create(header);
    lv_label_bind_text(vfo, vm_.vfo_text_subject(), nullptr);
    lv_obj_set_style_text_font(vfo, mono_font ? mono_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(vfo, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    // Left of centre: the right side holds the battery badge + S-meter.
    lv_obj_align(vfo, LV_ALIGN_CENTER, -24, 0);

    auto* smeter_box = lv_obj_create(header);
    lv_obj_remove_style_all(smeter_box);
    lv_obj_set_size(smeter_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(smeter_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(smeter_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(smeter_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(smeter_box, 3, 0);
    lv_obj_align(smeter_box, LV_ALIGN_RIGHT_MID, -4, 0);

    battery_ = std::make_unique<view::widgets::BatteryBadge>(smeter_box);
    lv_obj_set_style_margin_right(battery_->obj(), 6, 0);

    auto* s_label = lv_label_create(smeter_box);
    lv_label_set_text(s_label, "S");
    lv_obj_set_style_text_font(s_label, small_font ? small_font : &lv_font_montserrat_12, 0);
    reactive::bind_theme(s_label, vm_.dark_mode_subject(), reactive::ThemeRole::Text);

    auto* smeter = lv_bar_create(smeter_box);
    lv_obj_set_size(smeter, kSmeterWidth, 8);
    lv_bar_set_range(smeter, 0, 100);
    lv_obj_set_style_bg_color(smeter, view::palette(false).primary, LV_PART_INDICATOR);
    lv_obj_remove_flag(smeter, LV_OBJ_FLAG_CLICKABLE);
    reactive::bind_bar_value(smeter, vm_.smeter_subject());
    smeter_ = smeter;

    // Squelch level: a white tick across the S-meter, hidden while it is off.
    sq_marker_ = lv_obj_create(smeter);
    lv_obj_remove_style_all(sq_marker_);
    lv_obj_set_size(sq_marker_, 2, 8);
    lv_obj_set_style_bg_color(sq_marker_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sq_marker_, LV_OPA_COVER, 0);
    lv_obj_add_flag(sq_marker_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(sq_marker_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(sq_marker_, LV_OBJ_FLAG_CLICKABLE);

    // --- Spectrum line chart ---
    chart_ = lv_chart_create(content);
    lv_obj_set_size(chart_, LV_PCT(100), kChartHeight);
    lv_obj_set_style_pad_all(chart_, 0, 0);
    lv_obj_set_style_border_width(chart_, 0, 0);
    lv_obj_set_style_bg_opa(chart_, LV_OPA_TRANSP, 0);
    lv_chart_set_div_line_count(chart_, 0, 0);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_, kBins);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, kChartMax);
    lv_obj_set_style_size(chart_, 0, 0, LV_PART_INDICATOR); // hide point markers
    // Hairline traces: the theme default (2-3 px, rounded caps) smears the peaks.
    lv_obj_set_style_line_width(chart_, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart_, false, LV_PART_ITEMS);
    lv_obj_remove_flag(chart_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(chart_, LV_OBJ_FLAG_CLICKABLE);
    series_ = lv_chart_add_series(chart_, view::palette(false).primary, LV_CHART_AXIS_PRIMARY_Y);
    peak_series_ = lv_chart_add_series(chart_, lv_color_hex(0xffcc00), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_hide_series(chart_, peak_series_, true);
    peak_.assign(kBins, 0.0f);
    smooth_.assign(kBins, 0.0f);

    // --- Waterfall canvas (RGB565, full width) ---
    waterfall_ = lv_canvas_create(content);
    lv_canvas_set_buffer(waterfall_, wf_buf_.data(), kWaterfallWidth, kMaxWaterfallHeight,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(waterfall_, lv_color_black(), LV_OPA_COVER);
    lv_obj_set_size(waterfall_, kWaterfallWidth, kWaterfallHeight);
    lv_obj_remove_flag(waterfall_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(waterfall_, LV_OBJ_FLAG_CLICKABLE);

    // --- Reference grids (toggled from the visual page) ---
    build_grids(waterfall_);

    // --- Band labels (X axis): low/high edge frequencies overlaid on the
    // waterfall corners. Children of the canvas, so they cost no layout space.
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
        lv_obj_bind_flag_if_eq(lbl, vm_.freq_grid_subject(), LV_OBJ_FLAG_HIDDEN, 0);
    };
    add_grid_label(vm_.grid_low_text_subject(),  -kWaterfallWidth / 4);
    add_grid_label(vm_.grid_high_text_subject(),  kWaterfallWidth / 4);

    // --- Passband overlay: a translucent band highlighting the demod bandwidth.
    passband_ = lv_obj_create(content);
    lv_obj_remove_style_all(passband_);
    lv_obj_add_flag(passband_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(passband_, 2, kChartHeight + kRowPad + kWaterfallHeight);
    lv_obj_align(passband_, LV_ALIGN_TOP_LEFT, kWaterfallWidth / 2, kHeaderHeight + kRowPad);
    lv_obj_set_style_bg_color(passband_, view::palette(false).primary, 0);
    lv_obj_set_style_bg_opa(passband_, LV_OPA_30, 0);
    lv_obj_remove_flag(passband_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(passband_, LV_OBJ_FLAG_CLICKABLE);

    // --- Status label: top left of the spectrum, while a memory scan runs or
    // while calibrating (the calibration stops the scan).
    status_label_ = lv_label_create(content);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 2, kHeaderHeight + kRowPad);
    lv_obj_set_style_text_font(status_label_, small_font ? small_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_color(status_label_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(status_label_, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(status_label_, 3, 0);
    lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_CLICKABLE);
    lv_label_bind_text(status_label_, vm_.status_text_subject(), nullptr);
    status_text_observer_ = lv_subject_add_observer(vm_.status_text_subject(), status_text_cb, this);
    update_passband();

    // --- Centre frequency marker: a thin vertical line at the tuned VFO. ---
    auto* marker = lv_obj_create(content);
    lv_obj_remove_style_all(marker);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(marker, 1, kChartHeight + kRowPad + kWaterfallHeight);
    lv_obj_align(marker, LV_ALIGN_TOP_MID, 0, kHeaderHeight + kRowPad);
    lv_obj_set_style_bg_color(marker, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_60, 0);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);

    // Drive the live pipeline.
    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);

    // Open the frequency dialog when the ViewModel bumps the request counter.
    freq_req_observer_ = lv_subject_add_observer(vm_.freq_input_req_subject(), freq_req_cb, this);
    ppm_req_observer_ = lv_subject_add_observer(vm_.ppm_input_req_subject(), ppm_req_cb, this);
    memory_req_observer_ =
        lv_subject_add_observer(vm_.memory_req_subject(), memory_req_cb, this);
    toolbar_page_observer_ =
        lv_subject_add_observer(vm_.toolbar_page_subject(), toolbar_page_cb, this);

    // Waterfall/spectrum split (page-3 key 7). Fires immediately with the current
    // value, applying the initial split.
    wf_split_observer_ = lv_subject_add_observer(vm_.wf_split_subject(), wf_split_cb, this);
}

void SpectrumScreen::wf_split_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<SpectrumScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->apply_wf_split(lv_subject_get_int(subject));
    }
}

void SpectrumScreen::apply_wf_split(int32_t percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int32_t rows = percent * kMaxWaterfallHeight / 100;
    if (rows > kMaxWaterfallHeight) rows = kMaxWaterfallHeight;

    // Clear newly revealed rows so growing the waterfall doesn't expose stale
    // history left below the previous visible band.
    if (rows > wf_rows_ && !wf_buf_.empty()) {
        std::fill(wf_buf_.begin() + static_cast<size_t>(kWaterfallWidth) * wf_rows_,
                  wf_buf_.begin() + static_cast<size_t>(kWaterfallWidth) * rows,
                  uint16_t{0});
    }
    wf_rows_ = rows;

    if (waterfall_) {
        lv_obj_set_height(waterfall_, rows);
    }
    if (chart_) {
        int32_t chart_h = kSplitArea - kRowPad - rows;
        if (chart_h < 0) chart_h = 0;
        lv_obj_set_height(chart_, chart_h);
    }
}

void SpectrumScreen::freq_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<SpectrumScreen*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    const int v = lv_subject_get_int(subject);
    if (v != self->last_freq_req_) {  // ignores the initial (0) notification
        self->last_freq_req_ = v;
        self->open_freq_dialog();
    }
}

void SpectrumScreen::ppm_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<SpectrumScreen*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    const int v = lv_subject_get_int(subject);
    if (v != self->last_ppm_req_) {  // ignores the initial (0) notification
        self->last_ppm_req_ = v;
        self->open_freq_dialog(/*ppm=*/true);
    }
}

void SpectrumScreen::toolbar_page_cb(lv_observer_t* observer, lv_subject_t* subject) {
    if (auto* self = static_cast<SpectrumScreen*>(lv_observer_get_user_data(observer))) {
        self->vm_.on_toolbar_page(lv_subject_get_int(subject));
    }
}

void SpectrumScreen::memory_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<SpectrumScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int v = lv_subject_get_int(subject);
    if (v == self->last_memory_req_) return; // the initial (0) notification
    self->last_memory_req_ = v;
    const auto& d = self->vm_.memory_draft();
    self->memory_dialog_ = std::make_unique<toolkit::MemoryDialog>(
        self->assets(), self->vm_.is_dark_mode(), "Save memory",
        toolkit::memory_info(d.hz, d.mode), toolkit::memory_label(d.hz, d.mode),
        toolkit::kSavedGroup, [self](const std::string& name, const std::string& group) {
            self->vm_.save_memory(name, group);
        });
}

void SpectrumScreen::freq_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<SpectrumScreen*>(ctx)) {
        self->on_freq_key(key);
    }
}

void SpectrumScreen::open_freq_dialog(bool ppm) {
    if (freq_dialog_) {
        return;
    }
    freq_buf_.clear();
    dialog_ppm_ = ppm;

    auto* value_font = assets().load_font("inter-semibold.ttf", 20);
    auto* small_font = assets().load_font("inter-regular.ttf", 12);
    const auto* fb = &lv_font_montserrat_12;

    // Full-screen modal scrim on the top layer (above the overlay NavBar).
    freq_dialog_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(freq_dialog_);
    lv_obj_set_size(freq_dialog_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(freq_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(freq_dialog_, LV_OPA_80, 0);
    lv_obj_clear_flag(freq_dialog_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(freq_dialog_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(freq_dialog_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(freq_dialog_, 6, 0);

    auto* title = lv_label_create(freq_dialog_);
    lv_label_set_text(title, ppm ? "Frequency correction (ppm)" : "Frequency (MHz)");
    lv_obj_set_style_text_font(title, small_font ? small_font : fb, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    freq_value_label_ = lv_label_create(freq_dialog_);
    lv_obj_set_style_text_font(freq_value_label_, value_font ? value_font : fb, 0);
    lv_obj_set_style_text_color(freq_value_label_, view::palette(false).primary, 0);

    if (ppm) {
        freq_info_label_ = lv_label_create(freq_dialog_);
        lv_obj_set_style_text_font(freq_info_label_, small_font ? small_font : fb, 0);
        lv_obj_set_style_text_color(freq_info_label_, lv_color_white(), 0);
    }

    auto* hint = lv_label_create(freq_dialog_);
    lv_label_set_text(hint, ppm ? "- = sign    Enter = OK    Esc = cancel"
                                : "Enter = OK    Esc = cancel");
    lv_obj_set_style_text_font(hint, small_font ? small_font : fb, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xaaaaaa), 0);

    update_freq_label();
    // The ppm entry needs the minus key, which only a text capture delivers.
    // Digits, dot, Enter, Esc and Backspace arrive the same way in both modes.
    platform::set_key_capture(freq_key_cb, this, /*text=*/ppm);
}

void SpectrumScreen::update_freq_label() {
    if (freq_value_label_) {
        lv_label_set_text(freq_value_label_, freq_buf_.empty() ? "_" : freq_buf_.c_str());
    }
    if (freq_info_label_) {
        // How far the typed value (or the current one, before typing) moves
        // the signals on screen at this frequency: a positive correction moves
        // them up, to the right.
        double ppm = vm_.ppm_tenths() / 10.0;
        if (!freq_buf_.empty() && freq_buf_ != "-") {
            ppm = std::strtod(freq_buf_.c_str(), nullptr);
        }
        const double hz = static_cast<double>(vm_.vfo_hz()) * ppm * 1e-6;
        lv_label_set_text_fmt(freq_info_label_, "%s%.1f ppm moves signals %+d Hz at %.3f MHz",
                              freq_buf_.empty() ? "now " : "", ppm,
                              static_cast<int>(std::lround(hz)), vm_.vfo_hz() / 1e6);
    }
}

void SpectrumScreen::on_freq_key(uint32_t key) {
    if (dialog_ppm_ && key == '-') {
        // Toggle the sign.
        if (!freq_buf_.empty() && freq_buf_[0] == '-') freq_buf_.erase(0, 1);
        else freq_buf_.insert(0, 1, '-');
        update_freq_label();
        return;
    }
    if (key >= '0' && key <= '9') {
        const auto dot = freq_buf_.find('.');
        // MHz: 4 decimals (100 Hz). ppm: 1 decimal and up to 200.0.
        const size_t max_frac = dialog_ppm_ ? 1 : 4;
        const size_t max_len = dialog_ppm_ ? 6 : 9;
        const bool frac_full =
            dot != std::string::npos && (freq_buf_.size() - dot - 1) >= max_frac;
        if (!frac_full && freq_buf_.size() < max_len) {
            freq_buf_.push_back(static_cast<char>(key));
            update_freq_label();
        }
    } else if (key == '.') {
        if (freq_buf_.find('.') == std::string::npos) {
            if (freq_buf_.empty() || freq_buf_ == "-") {
                freq_buf_ += "0"; // allow a leading dot: ".5" -> "0.5"
            }
            freq_buf_.push_back('.');
            update_freq_label();
        }
    } else if (key == LV_KEY_BACKSPACE) {
        if (!freq_buf_.empty()) {
            freq_buf_.pop_back();
            update_freq_label();
        }
    } else if (key == LV_KEY_ENTER) {
        close_freq_dialog(true);
    } else if (key == LV_KEY_ESC) {
        close_freq_dialog(false);
    }
}

void SpectrumScreen::close_freq_dialog(bool apply) {
    platform::set_key_capture(nullptr, nullptr);

    if (apply && !freq_buf_.empty()) {
        char* end = nullptr;
        const double value = std::strtod(freq_buf_.c_str(), &end);
        if (end != freq_buf_.c_str()) {
            if (dialog_ppm_) {
                // set_ppm_tenths clamps to the supported range.
                vm_.set_ppm_tenths(static_cast<int>(std::lround(value * 10.0)));
            } else if (value > 0.0) {
                vm_.set_vfo_hz(static_cast<int64_t>(std::llround(value * 1000000.0)));
            }
        }
    }

    if (freq_dialog_) {
        lv_obj_delete(freq_dialog_);
        freq_dialog_ = nullptr;
    }
    freq_value_label_ = nullptr;
    freq_info_label_ = nullptr;
    freq_buf_.clear();
    dialog_ppm_ = false;
}

void SpectrumScreen::build_grids(lv_obj_t* parent) {
    const int32_t w = kWaterfallWidth;
    const int32_t h = kWaterfallHeight;

    const auto make_grid = [&](bool vertical, lv_subject_t* toggle) -> lv_obj_t* {
        auto* grid = lv_obj_create(parent);
        lv_obj_remove_style_all(grid);
        lv_obj_set_size(grid, w, h);
        lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_CLICKABLE);

        for (int k = 1; k < 4; ++k) {
            auto* line = lv_obj_create(grid);
            lv_obj_remove_style_all(line);
            if (vertical) {
                lv_obj_set_size(line, 1, h);
                lv_obj_align(line, LV_ALIGN_TOP_LEFT, k * w / 4, 0);
            } else {
                lv_obj_set_size(line, w, 1);
                lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, k * h / 4);
            }
            lv_obj_set_style_bg_color(line, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
        }

        lv_obj_bind_flag_if_eq(grid, toggle, LV_OBJ_FLAG_HIDDEN, 0);
        return grid;
    };

    // Only the vertical (frequency) grid is a static overlay. The horizontal
    // (time) grid is drawn as 1 s marker rows scrolling with the waterfall.
    freq_grid_ = make_grid(true, vm_.freq_grid_subject());
}

void SpectrumScreen::update_peak() {
    if (!chart_ || !peak_series_) {
        return;
    }
    const bool ph = vm_.peak_hold();
    if (ph != peak_visible_) {
        peak_visible_ = ph;
        lv_chart_hide_series(chart_, peak_series_, !ph);
        if (!ph) {
            std::fill(peak_.begin(), peak_.end(), 0.0f);
        }
    }
    if (!ph) {
        return;
    }
    int32_t* py = lv_chart_get_series_y_array(chart_, peak_series_);
    if (!py) {
        return;
    }
    for (int i = 0; i < kBins; ++i) {
        float held = peak_[i] * kPeakDecay; // decay, then hold the new maximum
        if (frame_[i] > held) {
            held = frame_[i];
        }
        peak_[i] = held;
        py[i] = static_cast<int32_t>(held * static_cast<float>(kChartMax));
    }
}

void SpectrumScreen::update_passband() {
    if (!passband_) {
        return;
    }
    const auto pb = vm_.passband();
    const double span_hz = static_cast<double>(vm_.span_khz()) * 1000.0;
    if (span_hz <= 0.0) {
        return;
    }
    const double hz_per_px = span_hz / static_cast<double>(kWaterfallWidth);
    const double center = kWaterfallWidth / 2.0;
    double left = center + static_cast<double>(pb.low_hz) / hz_per_px;
    double right = center + static_cast<double>(pb.high_hz) / hz_per_px;
    if (left < 0.0) left = 0.0;
    if (right > kWaterfallWidth) right = kWaterfallWidth;

    int x = static_cast<int>(left + 0.5);
    int w = static_cast<int>(right - left + 0.5);
    if (w < 2) w = 2; // keep narrow modes (SSB/CW) at least visible

    if (x != last_pb_x_ || w != last_pb_w_) {
        last_pb_x_ = x;
        last_pb_w_ = w;
        lv_obj_set_width(passband_, w);
        lv_obj_align(passband_, LV_ALIGN_TOP_LEFT, x, kHeaderHeight + kRowPad);
    }
}

void SpectrumScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<SpectrumScreen*>(lv_timer_get_user_data(timer));
    if (self) {
        self->tick();
    }
}

void SpectrumScreen::tick() {
    if (!source_) {
        return;
    }

    // Keep the real receiver tuned to the VFO/zoom span and the audio path on mode.
    // The tuner gets the VFO with the frequency correction applied (calibration).
    source_->set_tuning(vm_.tuner_hz(), static_cast<int32_t>(vm_.span_khz()) * 1000);
    source_->set_mode(static_cast<int>(vm_.mode()));
    source_->set_volume(vm_.volume_gain());
    source_->set_gain(vm_.gain_auto(), vm_.gain_tenth_db());

    source_->next_frame(frame_.data(), kBins);

    // The level in the tuned channel drives the S-meter and the squelch. A
    // closed squelch mutes the audio like the mute key, without changing it.
    const auto pb = vm_.passband();
    const int32_t span = vm_.span_khz() * 1000;
    const double frame_span = static_cast<double>(span);
    const auto now = static_cast<int64_t>(lv_tick_get());
    const int level = channel_level(frame_.data(), kBins, frame_span, pb.low_hz, pb.high_hz);
    bool open = vm_.update_channel_level(level, now);
    // A memory scan reads its channels from this frame and may retune (the
    // source follows on the next tick). Between channels it keeps quiet.
    const int64_t vfo_before = vm_.vfo_hz();
    vm_.scan_tick(frame_.data(), kBins, frame_span, source_->captured_span_hz(span), now);
    // A retune just reset the squelch: the level read above belongs to the old
    // frequency, so keep this frame quiet.
    if (vm_.vfo_hz() != vfo_before) open = false;
    open = open && !vm_.scan_mutes_audio();
    source_->set_muted(vm_.muted() || !open);
    update_squelch_view(open);

    // Feed the chart: scale [0,1] magnitudes to the integer Y array in place.
    if (chart_ && series_) {
        int32_t* y = lv_chart_get_series_y_array(chart_, series_);
        if (y) {
            for (int i = 0; i < kBins; ++i) {
                smooth_[i] += kSpectrumSmooth * (frame_[i] - smooth_[i]); // EMA
                y[i] = static_cast<int32_t>(smooth_[i] * static_cast<float>(kChartMax));
            }
        }
        update_peak();
        lv_chart_refresh(chart_);
    }

    push_waterfall_row(frame_.data());
    update_passband();
}

void SpectrumScreen::status_text_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<SpectrumScreen*>(lv_observer_get_user_data(observer));
    if (!self || !self->status_label_) return;
    const char* text = lv_subject_get_string(subject);
    if (text && text[0] != '\0') lv_obj_remove_flag(self->status_label_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(self->status_label_, LV_OBJ_FLAG_HIDDEN);
}

void SpectrumScreen::update_squelch_view(bool open) {
    if (!smeter_ || !sq_marker_) {
        return;
    }
    const int want = vm_.squelch_on() ? vm_.squelch_threshold() : -1;
    if (want != sq_marker_level_) {
        sq_marker_level_ = want;
        if (want < 0) {
            lv_obj_add_flag(sq_marker_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_pos(sq_marker_, (kSmeterWidth - 2) * want / 100, 0);
            lv_obj_remove_flag(sq_marker_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // Grey while the squelch holds the audio back, green otherwise.
    const int shown_open = open ? 1 : 0;
    if (shown_open != sq_shown_open_) {
        sq_shown_open_ = shown_open;
        lv_obj_set_style_bg_color(smeter_, open ? view::palette(false).primary : lv_color_hex(0x808080),
                                  LV_PART_INDICATOR);
    }
}

void SpectrumScreen::push_waterfall_row(const float* mags) {
    if (!waterfall_ || wf_buf_.empty() || wf_rows_ <= 0) {
        return; // 0% split: nothing visible, skip the per-frame work entirely
    }

    // Scroll the WHOLE buffer (kMaxWaterfallHeight), not just the visible
    // rows: the canvas can show rows below the scrolled band (especially
    // right after the split shrinks), and a partial scroll would leave that
    // lower band frozen with stale data. The extra memmove is negligible on
    // the CM0 (~3 MB/s).
    view::push_waterfall_row(wf_buf_.data(), kWaterfallWidth, kMaxWaterfallHeight, mags);

    lv_obj_invalidate(waterfall_);
}

} // namespace sdr
