/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "run_app.h"
#include "sdr_viewmodel.h"
#include "spectrum_screen.h"

#include <memory>

int main() {
    app::AssetManager assets;

    sdr::SdrViewModel view_model; // also the NavProvider (set on itself in ctor)

    std::unique_ptr<sdr::SpectrumScreen> screen;
    return toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        screen = std::make_unique<sdr::SpectrumScreen>(view_model, assets);
        return screen->root();
    });
}
