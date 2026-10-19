/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "handoff.h"
#include "memories_screen.h"
#include "memories_viewmodel.h"
#include "run_app.h"

#include <memory>

int main() {
    memories::MemoriesViewModel view_model;
    int rc = 0;
    {
        app::AssetManager assets;
        std::unique_ptr<memories::MemoriesScreen> screen;
        rc = toolkit::run_app(
            view_model, assets,
            [&]() -> lv_obj_t* {
                screen = std::make_unique<memories::MemoriesScreen>(view_model, assets);
                return screen->root();
            },
            [&]() { screen.reset(); });
    }
    if (view_model.handoff().pending()) return toolkit::run_handoff(view_model);
    return rc;
}
