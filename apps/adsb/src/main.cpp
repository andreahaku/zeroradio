/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "adsb_screen.h"
#include "adsb_viewmodel.h"
#include "aircraft.h"
#include "app_config.h"
#include "asset_manager.h"
#include "entity_store.h"
#include "file_json_source.h"
#include "run_app.h"

#include <cstdlib>
#include <memory>
#include <string>

#ifndef APP_MOCK_JSON_PATH
#define APP_MOCK_JSON_PATH "apps/adsb/assets/mock/aircraft.json"
#endif

int main() {
    app::AssetManager assets;

    adsb::AdsbViewModel view_model; // also the NavProvider (set on itself in ctor)

    toolkit::EntityStore store;
    toolkit::Config config; // HOME (Milano area) + TTL; matches the mock

    // Resolve the JSON source: ADSB_JSON env overrides the bundled mock file.
    std::string json_path = APP_MOCK_JSON_PATH;
    if (const char* env = std::getenv("ADSB_JSON"); env && env[0] != '\0') {
        json_path = env;
    }

    // Background poller: parse aircraft.json on the reader thread and merge into
    // the (locked) store. The screen reads snapshots on its own timer.
    toolkit::FileJsonSource source(
        json_path,
        [&store](const std::string& json) {
            const auto aircraft = adsb::parse_aircraft_json(json);
            adsb::apply_to_store(store, aircraft);
        },
        2000);

    source.start();

    std::unique_ptr<adsb::AdsbScreen> screen;
    const int rc = toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        // The header's conn dot polls the live source state each tick.
        screen = std::make_unique<adsb::AdsbScreen>(
            view_model, assets, store, config, [&source]() { return source.ok(); });
        return screen->root();
    });

    source.stop();
    return rc;
}
