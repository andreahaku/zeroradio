/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ais_decoder.h"
#include "entity_store.h"

#include <string>

namespace ais {

// Merge one decoded Vessel into the shared EntityStore (keyed by MMSI), mirroring
// ADS-B's apply_to_store: a sparse field bag, position only when available.
// Position (1/2/3) and static (type 5) reports merge into the same record.
void apply_to_store(toolkit::EntityStore& store, const Vessel& vessel);

// Feed a blob of newline-separated !AIVDM sentences through `re` (which persists
// the partial state of multi-fragment messages across calls) and merge every
// completed message into the store. Use one AivdmReassembler per source so that
// fragments split across reads/datagrams still reassemble.
void apply_nmea(AivdmReassembler& re, toolkit::EntityStore& store, const std::string& nmea);

} // namespace ais
