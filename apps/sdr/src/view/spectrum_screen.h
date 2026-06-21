/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "sdr_viewmodel.h"

#include "lvgl.h"

namespace sdr {

// The SDR receiver screen. Scaffold: an empty body under a solid NavBar, proving
// the toolkit shell drives a second app. The FFT chart + RGB565 waterfall +
// S-meter + frequency dialog are ported from SDRTerminal's spectrum_screen in a
// later step (refit to 320x170). See docs/sdrterminal-port-plan.md.
class SpectrumScreen : public screen::BaseScreen {
public:
    SpectrumScreen(SdrViewModel& vm, app::AssetManager& assets);
    ~SpectrumScreen() override;

protected:
    void build_content(lv_obj_t* content) override;
    bool show_title_bar() const override { return false; }
    bool overlay_nav_bar() const override { return false; }

private:
    SdrViewModel& vm_;
    lv_obj_t* placeholder_ = nullptr;
};

} // namespace sdr
