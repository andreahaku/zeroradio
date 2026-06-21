/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "spectrum_screen.h"

#include "bindings.h"
#include "theme.h"

namespace sdr {

SpectrumScreen::SpectrumScreen(SdrViewModel& vm, app::AssetManager& assets)
    : BaseScreen(vm, vm, assets), vm_(vm) {
    init();
}

SpectrumScreen::~SpectrumScreen() = default;

void SpectrumScreen::build_content(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, 0, 0);

    // Scaffold placeholder until the spectrum/waterfall pipeline is ported.
    placeholder_ = lv_label_create(content);
    lv_label_set_text(placeholder_, "SDR\n(spectrum scaffold)");
    lv_obj_set_style_text_align(placeholder_, LV_TEXT_ALIGN_CENTER, 0);
    reactive::bind_theme(placeholder_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
}

} // namespace sdr
