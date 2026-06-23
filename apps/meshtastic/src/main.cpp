/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "meshtastic_client_source.h"
#include "meshtastic_screen.h"
#include "meshtastic_viewmodel.h"
#include "proto_smoke.h"
#include "run_app.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

int main() {
    // Step-1 plumbing check (radio-apps/09b): the vendored nanopb + Meshtastic
    // protobuf stack links and decodes.
    std::fprintf(stderr, "[meshtastic] proto smoke decode: %s\n",
                 meshtastic::proto_smoke_decode() ? "ok" : "FAILED");

    // Step-2 (radio-apps/09b): connect to the local meshtasticd Client API and
    // run the handshake. Endpoint overridable via env for testing.
    std::string host = "127.0.0.1";
    uint16_t port = 4403;
    if (const char* h = std::getenv("MESHTASTICD_HOST"); h && h[0] != '\0') host = h;
    if (const char* p = std::getenv("MESHTASTICD_PORT"); p && p[0] != '\0') {
        const int v = std::atoi(p);
        if (v > 0 && v < 65536) port = static_cast<uint16_t>(v);
    }

    meshtastic::MeshtasticClientSource source(
        host, port,
        meshtastic::MeshtasticClientSource::Callbacks{
            [](uint32_t num) { std::fprintf(stderr, "[meshtastic] self node: 0x%08x\n", num); },
            [](int nodes) { std::fprintf(stderr, "[meshtastic] nodedb synced: %d nodes\n", nodes); },
        });
    source.start();

    app::AssetManager assets;

    meshtastic::MeshtasticViewModel view_model; // also the NavProvider (set in ctor)

    std::unique_ptr<meshtastic::MeshtasticScreen> screen;
    const int rc = toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        screen = std::make_unique<meshtastic::MeshtasticScreen>(view_model, assets);
        return screen->root();
    });

    source.stop();
    return rc;
}
