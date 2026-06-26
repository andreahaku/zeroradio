/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_screen.h"
#include "ais_viewmodel.h"
#include "asset_manager.h"
#include "entity_store.h"
#include "file_json_source.h"
#include "run_app.h"
#include "vessel_store.h"

#include <cstdlib>
#include <memory>
#include <string>

#ifndef APP_MOCK_NMEA_PATH
#define APP_MOCK_NMEA_PATH "apps/ais/assets/mock/ais.nmea"
#endif

// AIS viewer (Option: port of apps/adsb). Like ADS-B reading dump1090's decoded
// aircraft.json, this reads decoded !AIVDM sentences produced on a host by
// rtl_ais / AIS-catcher; the on-device decode (parse_aivdm) turns each sentence
// into a Vessel and merges it into the shared EntityStore. AIS_NMEA overrides the
// bundled mock file.
int main() {
    app::AssetManager assets;

    ais::AisViewModel view_model;
    toolkit::EntityStore store;

    std::string nmea_path = APP_MOCK_NMEA_PATH;
    if (const char* env = std::getenv("AIS_NMEA"); env && env[0] != '\0') {
        nmea_path = env;
    }

    // Reuse the toolkit's file poller (same lifecycle as ADS-B's): it reads the
    // whole NMEA file each tick; we split it into sentences and decode each.
    toolkit::FileJsonSource source(
        nmea_path,
        [&store](const std::string& nmea) {
            const auto vessels = ais::parse_nmea_lines(nmea);
            ais::apply_to_store(store, vessels);
        },
        2000);
    source.start();

    std::unique_ptr<ais::AisScreen> screen;
    const int rc = toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        screen = std::make_unique<ais::AisScreen>(
            view_model, assets, store, [&source]() { return source.ok(); });
        return screen->root();
    });

    source.stop();
    return rc;
}
