/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_screen.h"
#include "ais_viewmodel.h"
#include "app_config.h"
#include "asset_manager.h"
#include "child_service.h"
#include "entity_store.h"
#include "file_json_source.h"
#include "nmea_net_source.h"
#include "run_app.h"
#include "vessel_store.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
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
namespace {

bool env_set(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] != '\0';
}

bool source_is_mock() {
    const char* v = std::getenv("AIS_SOURCE");
    return v && std::strcmp(v, "mock") == 0;
}

} // namespace

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

    // One reassembler per source: type 5 (name/type) spans two fragments, which
    // must be joined before decoding. Read on the source thread only.
    ais::AivdmReassembler reassembler;
    auto on_nmea = [&store, &reassembler](const std::string& nmea) {
        ais::apply_nmea(reassembler, store, nmea);
    };

    // Source selection:
    //   unset                 -> the dongle on this device: our own AIS-catcher
    //                            sends NMEA to UDP 127.0.0.1:10110 (stopped on exit)
    //   AIS_UDP=<port>        -> bind a UDP port fed by an external decoder
    //   AIS_TCP=<host>:<port> -> connect TCP (AIS-catcher / aggregators)
    //   AIS_NMEA=<file>       -> poll a file of !AIVDM lines
    //   AIS_SOURCE=mock       -> the bundled sample file
    std::unique_ptr<toolkit::ChildService> decoder;
    std::unique_ptr<toolkit::NmeaNetSource> net_source;
    std::unique_ptr<toolkit::FileJsonSource> file_source;
    std::function<bool()> conn_state;

    if (const char* udp = std::getenv("AIS_UDP"); udp && udp[0] != '\0') {
        const auto port = static_cast<uint16_t>(std::atoi(udp));
        net_source = std::make_unique<toolkit::NmeaNetSource>(
            toolkit::NmeaNetSource::Protocol::Udp, "", port, on_nmea);
    } else if (const char* tcp = std::getenv("AIS_TCP"); tcp && tcp[0] != '\0') {
        std::string spec = tcp; // host:port
        std::string host = "127.0.0.1";
        uint16_t port = 0;
        if (const auto colon = spec.find(':'); colon != std::string::npos) {
            host = spec.substr(0, colon);
            port = static_cast<uint16_t>(std::atoi(spec.c_str() + colon + 1));
        } else {
            port = static_cast<uint16_t>(std::atoi(spec.c_str()));
        }
        net_source = std::make_unique<toolkit::NmeaNetSource>(
            toolkit::NmeaNetSource::Protocol::Tcp, host, port, on_nmea);
    } else if (!env_set("AIS_NMEA") && !source_is_mock()) {
        constexpr uint16_t kLocalPort = 10110;
        // -X off: never share received data with the aiscatcher.org feed.
        decoder = std::make_unique<toolkit::ChildService>(std::vector<std::string>{
            toolkit::find_tool("AIS-catcher"), "-d:0", "-X", "off",
            "-u", "127.0.0.1", std::to_string(kLocalPort)});
        net_source = std::make_unique<toolkit::NmeaNetSource>(
            toolkit::NmeaNetSource::Protocol::Udp, "", kLocalPort, on_nmea);
    }

    if (net_source) {
        net_source->start();
        conn_state = [src = net_source.get()]() { return src->ok(); };
    } else {
        std::string nmea_path = APP_MOCK_NMEA_PATH;
        if (const char* env = std::getenv("AIS_NMEA"); env && env[0] != '\0') {
            nmea_path = env;
        }
        file_source = std::make_unique<toolkit::FileJsonSource>(nmea_path, on_nmea, 2000);
        file_source->start();
        conn_state = [src = file_source.get()]() { return src->ok(); };
    }

    std::unique_ptr<ais::AisScreen> screen;
    const int rc = toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        screen = std::make_unique<ais::AisScreen>(view_model, assets, store, config, conn_state);
        return screen->root();
    });

    if (net_source) net_source->stop();
    if (file_source) file_source->stop();
    return rc;
}
