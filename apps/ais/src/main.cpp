/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_screen.h"
#include "ais_viewmodel.h"
#include "app_config.h"
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

// AIS viewer (port of apps/adsb). Like ADS-B reading dump1090's decoded
// aircraft.json, this reads decoded !AIVDM sentences produced on a host by
// rtl_ais / AIS-catcher; the on-device decode (parse_aivdm) turns each sentence
// into a Vessel and merges it into the shared EntityStore. AIS_NMEA overrides the
// bundled mock file; AIS_HOME_LAT/LON/AIS_TTL re-centre/age the radar.
int main() {
    app::AssetManager assets;

    ais::AisViewModel view_model; // also the NavProvider (set on itself in ctor)

    toolkit::EntityStore store;
    toolkit::Config config; // HOME + TTL; override via env below.

    if (const char* lat = std::getenv("AIS_HOME_LAT"); lat && lat[0] != '\0') {
        char* end = nullptr;
        const double v = std::strtod(lat, &end);
        if (end != lat && *end == '\0' && v >= -90.0 && v <= 90.0) config.home.lat = v;
    }
    if (const char* lon = std::getenv("AIS_HOME_LON"); lon && lon[0] != '\0') {
        char* end = nullptr;
        const double v = std::strtod(lon, &end);
        if (end != lon && *end == '\0' && v >= -180.0 && v <= 180.0) config.home.lon = v;
    }
    if (const char* ttl = std::getenv("AIS_TTL"); ttl && ttl[0] != '\0') {
        const double v = std::atof(ttl);
        if (v > 0.0) {
            config.ttl_seconds = v;
            view_model.set_ttl_seconds(v);
        }
    }

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
            view_model, assets, store, config, [&source]() { return source.ok(); });
        return screen->root();
    });

    source.stop();
    return rc;
}
