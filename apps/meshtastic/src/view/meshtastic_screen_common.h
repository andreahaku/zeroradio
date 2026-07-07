/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Shared private helpers for the MeshtasticScreen translation units
// (meshtastic_screen*.cpp). Not part of the app's public surface.

#include "entity_store.h"

#include <string>

namespace meshtastic {
namespace common {

inline std::string field_of(const toolkit::Entity& e, const char* key) {
    const auto it = e.fields.find(key);
    return it != e.fields.end() ? it->second : std::string{};
}

// MAP (PPI) geometry. Content area is 320x110 (title + nav bars take 30 each), so
// a 106px square canvas fits with a hair of margin; the flanking side columns
// carry the colour-coded node names.
constexpr int kMapSize = 106;
// The Mercator map view isn't bound to a circle, so it spreads to the full
// screen width (same height); the flanking short-name columns stay anchored to
// the screen edges and overlay the map with a transparent background (the
// selected node still renders inverted).
constexpr int kMapMercW = 320;
constexpr int kMapMercH = 106;

} // namespace common
} // namespace meshtastic
