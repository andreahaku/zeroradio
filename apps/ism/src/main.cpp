/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "entity_store.h"
#include "ism_screen.h"
#include "ism_source.h"
#include "ism_viewmodel.h"
#include "run_app.h"

#include <memory>

int main() {
    app::AssetManager assets;

    ism::IsmViewModel view_model; // also the NavProvider (set on itself in ctor)

    toolkit::EntityStore store;
    ism::IsmSource source(store); // rtl_433 / file / mock, chosen from the env
    source.start();

    std::unique_ptr<ism::IsmScreen> screen;
    const int rc = toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        screen = std::make_unique<ism::IsmScreen>(
            view_model, assets, store, [&source]() { return source.ok(); });
        return screen->root();
    });

    source.stop();
    return rc;
}
