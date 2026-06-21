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
    toolkit::Config config; // HOME (Bologna, IT) + TTL; override via env below.

    // Home position / TTL overrides so the radar centres on the user's location
    // without a rebuild: ADSB_HOME_LAT, ADSB_HOME_LON, ADSB_TTL.
    // strtod (not atof) so a typo like "abc" is rejected instead of silently
    // moving home to lat/lon 0 (the equator); also range-check the value.
    if (const char* lat = std::getenv("ADSB_HOME_LAT"); lat && lat[0] != '\0') {
        char* end = nullptr;
        const double v = std::strtod(lat, &end);
        if (end != lat && *end == '\0' && v >= -90.0 && v <= 90.0) config.home.lat = v;
    }
    if (const char* lon = std::getenv("ADSB_HOME_LON"); lon && lon[0] != '\0') {
        char* end = nullptr;
        const double v = std::strtod(lon, &end);
        if (end != lon && *end == '\0' && v >= -180.0 && v <= 180.0) config.home.lon = v;
    }
    if (const char* ttl = std::getenv("ADSB_TTL"); ttl && ttl[0] != '\0') {
        const double v = std::atof(ttl);
        if (v > 0.0) {
            config.ttl_seconds = v;
            // The sweep reads the viewmodel's TTL, not config; apply the env
            // override there too (it wins over the persisted setting for this run).
            view_model.set_ttl_seconds(v);
        }
    }

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
