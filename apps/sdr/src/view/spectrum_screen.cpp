/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "spectrum_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#ifdef SDR_HAVE_RTLTCP
#include "rtl_tcp_source.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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

// Peak-hold decay per tick (~33 ms): peaks fade over ~1-2 s ("temporary hold").
constexpr float kPeakDecay = 0.97f;

// Map a normalized magnitude [0,1] to an RGB565 colormap:
// black -> blue -> cyan -> yellow -> red.
uint16_t colormap_rgb565(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    float r, g, b;
    if (v < 0.25f) {            // black -> blue
        const float t = v / 0.25f;
        r = 0.0f; g = 0.0f; b = t;
    } else if (v < 0.5f) {      // blue -> cyan
        const float t = (v - 0.25f) / 0.25f;
        r = 0.0f; g = t; b = 1.0f;
    } else if (v < 0.75f) {     // cyan -> yellow
        const float t = (v - 0.5f) / 0.25f;
        r = t; g = 1.0f; b = 1.0f - t;
    } else {                    // yellow -> red
        const float t = (v - 0.75f) / 0.25f;
        r = 1.0f; g = 1.0f - t; b = 0.0f;
    }

    const auto to_u8 = [](float c) -> uint8_t {
        const int x = static_cast<int>(c * 255.0f + 0.5f);
        return static_cast<uint8_t>(std::clamp(x, 0, 255));
    };
    return lv_color_to_u16(lv_color_make(to_u8(r), to_u8(g), to_u8(b)));
}

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
// The rtl_tcp endpoint can be overridden with `SDR_RTLTCP=host:port`.
std::unique_ptr<SpectrumSource> make_source() {
#ifdef SDR_HAVE_RTLTCP
    const char* want = std::getenv("SDR_SOURCE");
    if (!want || std::strcmp(want, "mock") != 0) {
        std::string host = "127.0.0.1";
        uint16_t    port = 1234;
        if (const char* ep = std::getenv("SDR_RTLTCP"); ep && ep[0] != '\0') {
            const std::string s = ep;
            const auto colon = s.find(':');
            if (colon != std::string::npos) {
                host = s.substr(0, colon);
                port = static_cast<uint16_t>(std::atoi(s.c_str() + colon + 1));
            } else {
                host = s;
            }
        }
        return std::make_unique<RtlTcpSource>(host, port, kDefaultCenterHz);
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
      wf_buf_(static_cast<size_t>(kWaterfallWidth) * kWaterfallHeight, 0u) {
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
    lv_obj_align(vfo, LV_ALIGN_CENTER, 0, 0);

    auto* smeter_box = lv_obj_create(header);
    lv_obj_remove_style_all(smeter_box);
    lv_obj_set_size(smeter_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(smeter_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(smeter_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(smeter_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(smeter_box, 3, 0);
    lv_obj_align(smeter_box, LV_ALIGN_RIGHT_MID, -4, 0);

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
    lv_obj_remove_flag(chart_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(chart_, LV_OBJ_FLAG_CLICKABLE);
    series_ = lv_chart_add_series(chart_, view::palette(false).primary, LV_CHART_AXIS_PRIMARY_Y);
    peak_series_ = lv_chart_add_series(chart_, lv_color_hex(0xffcc00), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_hide_series(chart_, peak_series_, true);
    peak_.assign(kBins, 0.0f);

    // --- Waterfall canvas (RGB565, full width) ---
    waterfall_ = lv_canvas_create(content);
    lv_canvas_set_buffer(waterfall_, wf_buf_.data(), kWaterfallWidth, kWaterfallHeight,
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

void SpectrumScreen::freq_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<SpectrumScreen*>(ctx)) {
        self->on_freq_key(key);
    }
}

void SpectrumScreen::open_freq_dialog() {
    if (freq_dialog_) {
        return;
    }
    freq_buf_.clear();

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
    lv_label_set_text(title, "Frequency (MHz)");
    lv_obj_set_style_text_font(title, small_font ? small_font : fb, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    freq_value_label_ = lv_label_create(freq_dialog_);
    lv_obj_set_style_text_font(freq_value_label_, value_font ? value_font : fb, 0);
    lv_obj_set_style_text_color(freq_value_label_, view::palette(false).primary, 0);

    auto* hint = lv_label_create(freq_dialog_);
    lv_label_set_text(hint, "Enter = OK    Esc = cancel");
    lv_obj_set_style_text_font(hint, small_font ? small_font : fb, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xaaaaaa), 0);

    update_freq_label();
    platform::set_key_capture(freq_key_cb, this);
}

void SpectrumScreen::update_freq_label() {
    if (freq_value_label_) {
        lv_label_set_text(freq_value_label_, freq_buf_.empty() ? "_" : freq_buf_.c_str());
    }
}

void SpectrumScreen::on_freq_key(uint32_t key) {
    if (key >= '0' && key <= '9') {
        const auto dot = freq_buf_.find('.');
        const bool frac_full =
            dot != std::string::npos && (freq_buf_.size() - dot - 1) >= 4;
        if (!frac_full && freq_buf_.size() < 9) {
            freq_buf_.push_back(static_cast<char>(key));
            update_freq_label();
        }
    } else if (key == '.') {
        if (freq_buf_.find('.') == std::string::npos) {
            if (freq_buf_.empty()) {
                freq_buf_ = "0"; // allow a leading dot: ".5" -> "0.5"
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
        const double mhz = std::strtod(freq_buf_.c_str(), &end);
        if (end != freq_buf_.c_str() && mhz > 0.0) {
            vm_.set_vfo_hz(static_cast<int64_t>(std::llround(mhz * 1000000.0)));
        }
    }

    if (freq_dialog_) {
        lv_obj_delete(freq_dialog_);
        freq_dialog_ = nullptr;
    }
    freq_value_label_ = nullptr;
    freq_buf_.clear();
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
    source_->set_tuning(vm_.vfo_hz(), static_cast<int32_t>(vm_.span_khz()) * 1000);
    source_->set_mode(static_cast<int>(vm_.mode()));
    source_->set_volume(vm_.volume_gain());
    source_->set_muted(vm_.muted());
    source_->set_gain(vm_.gain_auto(), vm_.gain_tenth_db());

    source_->next_frame(frame_.data(), kBins);

    // Feed the chart: scale [0,1] magnitudes to the integer Y array in place.
    if (chart_ && series_) {
        int32_t* y = lv_chart_get_series_y_array(chart_, series_);
        if (y) {
            for (int i = 0; i < kBins; ++i) {
                y[i] = static_cast<int32_t>(frame_[i] * static_cast<float>(kChartMax));
            }
        }
        update_peak();
        lv_chart_refresh(chart_);
    }

    push_waterfall_row(frame_.data());
    update_passband();

    vm_.set_smeter(static_cast<int>(source_->last_peak() * 100.0f));
}

void SpectrumScreen::push_waterfall_row(const float* mags) {
    if (!waterfall_ || wf_buf_.empty()) {
        return;
    }

    const int w = kWaterfallWidth;
    const int h = kWaterfallHeight;

    // Scroll everything down by one row (new data at the top, flowing downward).
    std::memmove(wf_buf_.data() + w, wf_buf_.data(),
                 static_cast<size_t>(w) * (h - 1) * sizeof(uint16_t));

    uint16_t* top = wf_buf_.data();
    for (int x = 0; x < w; ++x) {
        top[x] = colormap_rgb565(mags[x]);
    }

    // Time grid: stamp a translucent marker line every ~1 s that scrolls down.
    if (lv_subject_get_int(vm_.time_grid_subject()) != 0) {
        if (++marker_count_ >= kMarkerPeriodTicks) {
            marker_count_ = 0;
            for (int x = 0; x < w; ++x) {
                top[x] = blend_black(top[x]);
                if (h > 1) {
                    top[w + x] = blend_black(top[w + x]);
                }
            }
        }
    } else {
        marker_count_ = 0;
    }

    lv_obj_invalidate(waterfall_);
}

} // namespace sdr
