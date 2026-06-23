/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace meshtastic {

// Step-1 plumbing check (radio-apps/09b): decode an empty, all-optional
// FromRadio with the vendored nanopb runtime + generated Meshtastic types. It
// proves the protobuf stack compiles, links, and decodes on this target (host
// SDL build and the cp0 aarch64 cross build). Returns true on success.
bool proto_smoke_decode();

} // namespace meshtastic
