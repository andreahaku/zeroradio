/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace survey {

// Survey state persisted across runs (mirrors SdrModel): the swept range and
// the dark-mode flag. The generic shell state lives in toolkit::ShellViewModel.
class SurveyModel {
public:
    SurveyModel(); // loads the persisted session, if any

    bool dark_mode() const;
    void toggle_dark_mode();

    int64_t start_hz() const;
    int64_t stop_hz() const;

    // Resolution hint for the sweep backend: the span divided across the
    // canvas columns, clamped to what the tools accept.
    int32_t bin_hz() const;

    void zoom_in();      // halve the span around the centre
    void zoom_out();     // double the span around the centre

    // Manual tune: set the swept window from a centre frequency and total span
    // (start = centre - span/2, stop = centre + span/2), clamped to the radio
    // limits and the minimum span. Persists.
    void set_center_span(int64_t center_hz, int64_t span_hz);

private:
    void load_state();
    void save_state() const;
    void set_range(int64_t start_hz, int64_t stop_hz);

    bool dark_mode_ = true;
    int64_t start_hz_ = 88000000;   // FM broadcast: guaranteed signals
    int64_t stop_hz_  = 108000000;
};

} // namespace survey
