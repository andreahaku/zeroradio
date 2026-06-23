/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "meshtastic_screen.h"
#include "meshtastic_viewmodel.h"
#include "proto_smoke.h"
#include "run_app.h"

#include <cstdio>
#include <memory>

int main() {
    // Step-1 plumbing check (radio-apps/09b): the vendored nanopb + Meshtastic
    // protobuf stack links and decodes. Real wire decode lands with the source.
    std::fprintf(stderr, "[meshtastic] proto smoke decode: %s\n",
                 meshtastic::proto_smoke_decode() ? "ok" : "FAILED");

    app::AssetManager assets;

    meshtastic::MeshtasticViewModel view_model; // also the NavProvider (set in ctor)

    std::unique_ptr<meshtastic::MeshtasticScreen> screen;
    const int rc = toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        screen = std::make_unique<meshtastic::MeshtasticScreen>(view_model, assets);
        return screen->root();
    });

    return rc;
}
