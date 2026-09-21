/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "handoff.h"
#include "run_app.h"
#include "sdr_viewmodel.h"
#include "spectrum_screen.h"

#include <memory>

int main() {
    sdr::SdrViewModel view_model; // also the NavProvider (set on itself in ctor)

    int rc = 0;
    {
        app::AssetManager assets;
        std::unique_ptr<sdr::SpectrumScreen> screen;
        // The teardown callback releases the display and input after the loop;
        // dropping the screen stops rtl_tcp and the audio, freeing the dongle
        // before a hand-off.
        rc = toolkit::run_app(
            view_model, assets,
            [&]() -> lv_obj_t* {
                screen = std::make_unique<sdr::SpectrumScreen>(view_model, assets);
                return screen->root();
            },
            [&]() { screen.reset(); });
    }

    // TAB: become the Survey app. Only returns on no hand-off or failure.
    if (view_model.handoff().pending()) return toolkit::run_handoff(view_model);
    return rc;
}
