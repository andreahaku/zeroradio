/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ais_decoder.h"
#include "entity_store.h"

#include <string>
#include <vector>

namespace ais {

// Decode every !AIVDM line in `nmea` (one sentence per line) into Vessels.
// Lines that are not a valid type 1/2/3 position report are skipped.
std::vector<Vessel> parse_nmea_lines(const std::string& nmea);

// Merge vessels into the shared EntityStore (keyed by MMSI), mirroring ADS-B's
// apply_to_store: sparse field bag, position only when available.
void apply_to_store(toolkit::EntityStore& store, const std::vector<Vessel>& vessels);

} // namespace ais
