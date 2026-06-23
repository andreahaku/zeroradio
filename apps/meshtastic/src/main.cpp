/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "meshtastic_screen.h"
#include "meshtastic_viewmodel.h"
#include "run_app.h"

#include <memory>

int main() {
    app::AssetManager assets;

    meshtastic::MeshtasticViewModel view_model; // also the NavProvider (set in ctor)

    std::unique_ptr<meshtastic::MeshtasticScreen> screen;
    const int rc = toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        screen = std::make_unique<meshtastic::MeshtasticScreen>(view_model, assets);
        return screen->root();
    });

    return rc;
}
