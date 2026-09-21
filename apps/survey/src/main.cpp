/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "run_app.h"
#include "survey_viewmodel.h"
#include "survey_screen.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>

int main() {
    survey::SurveyViewModel view_model; // also the NavProvider (set on itself in ctor)

    int rc = 0;
    {
        app::AssetManager assets;
        std::unique_ptr<survey::SurveyScreen> screen;
        // The teardown callback makes run_app release the display and input
        // after the loop; dropping the screen stops the sweep (rtl_power) and
        // frees the dongle. Both must be free before an SDR hand-off.
        rc = toolkit::run_app(
            view_model, assets,
            [&]() -> lv_obj_t* {
                screen = std::make_unique<survey::SurveyScreen>(view_model, assets);
                return screen->root();
            },
            [&]() { screen.reset(); });
    }

    // "Open in SDR": become the SDR app (same PID, so a waiting hub keeps
    // waiting), tuned to the selected peak. Only returns if exec fails.
    const std::string& sdr = view_model.handoff_sdr_path();
    if (!sdr.empty()) {
        const std::string freq = std::to_string(view_model.handoff_freq_hz());
        ::setenv("SDR_FREQ", freq.c_str(), 1);
        std::fflush(nullptr);
        ::execl(sdr.c_str(), sdr.c_str(), static_cast<char*>(nullptr));
        std::perror("[survey] exec sdr_app");
        return 1;
    }
    return rc;
}
