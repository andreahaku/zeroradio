/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"
#include "entity_store.h"
#include "message_log.h"
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

    // Nodes flow from the source's reader thread into the store; messages into the
    // log. The screen snapshots both on the UI thread (the toolkit's cross-thread
    // pattern).
    toolkit::EntityStore store;
    meshtastic::MessageLog messages;

    meshtastic::MeshtasticClientSource source(
        host, port,
        meshtastic::MeshtasticClientSource::Callbacks{
            [](uint32_t num) { std::fprintf(stderr, "[meshtastic] self node: 0x%08x\n", num); },
            [](int nodes) { std::fprintf(stderr, "[meshtastic] nodedb synced: %d nodes\n", nodes); },
            [&store](const meshtastic::NodeUpdate& u) {
                store.upsert(u.id, [&u](toolkit::Entity& e) {
                    if (u.has_long) e.fields["long"] = u.long_name;
                    if (u.has_short) e.fields["short"] = u.short_name;
                    if (u.has_snr) {
                        char b[16];
                        std::snprintf(b, sizeof(b), "%.1f", static_cast<double>(u.snr));
                        e.fields["snr"] = b;
                    }
                    if (u.has_hops) e.fields["hops"] = std::to_string(u.hops);
                    if (u.has_last_heard)
                        e.fields["last_heard"] = std::to_string(u.last_heard);
                    if (u.is_self) e.fields["self"] = "1";
                    if (u.has_pos) {
                        e.has_pos = true;
                        e.pos.lat = u.lat;
                        e.pos.lon = u.lon;
                    }
                });
            },
            [&messages](const meshtastic::MeshMessage& m) { messages.add(m); },
            [&messages](uint32_t id, meshtastic::AckState st) { messages.update_ack(id, st); },
        });
    source.start();

    app::AssetManager assets;

    meshtastic::MeshtasticViewModel view_model; // also the NavProvider (set in ctor)

    std::unique_ptr<meshtastic::MeshtasticScreen> screen;
    const int rc = toolkit::run_app(view_model, assets, [&]() -> lv_obj_t* {
        screen = std::make_unique<meshtastic::MeshtasticScreen>(
            view_model, assets, store, messages,
            [&source](const std::string& t) { return source.send_text(t); });
        return screen->root();
    });

    source.stop();
    return rc;
}
