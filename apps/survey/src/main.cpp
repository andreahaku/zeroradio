/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "handoff.h"
#include "run_app.h"
#include "survey_viewmodel.h"
#include "survey_screen.h"

#include <memory>

int main() {
    survey::SurveyViewModel view_model; // also the NavProvider (set on itself in ctor)

    int rc = 0;
    {
        app::AssetManager assets;
        std::unique_ptr<survey::SurveyScreen> screen;
        // The teardown callback makes run_app release the display and input
        // after the loop; dropping the screen stops the sweep (rtl_power) and
        // frees the dongle. Both must be free before a hand-off.
        rc = toolkit::run_app(
            view_model, assets,
            [&]() -> lv_obj_t* {
                screen = std::make_unique<survey::SurveyScreen>(view_model, assets);
                return screen->root();
            },
            [&]() { screen.reset(); });
    }

    // Open in SDR / TAB: become the SDR app. Only returns on no hand-off or failure.
    if (view_model.handoff().pending()) return toolkit::run_handoff(view_model);
    return rc;
}
