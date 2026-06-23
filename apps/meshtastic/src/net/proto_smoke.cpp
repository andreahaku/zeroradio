/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "proto_smoke.h"

#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"

namespace meshtastic {

bool proto_smoke_decode() {
    // A zero-length buffer is a valid protobuf message: every field stays at its
    // default. Decoding it exercises the generated descriptor + the runtime.
    meshtastic_FromRadio msg = meshtastic_FromRadio_init_zero;
    const uint8_t buf[1] = {0};
    pb_istream_t stream = pb_istream_from_buffer(buf, 0);
    return pb_decode(&stream, meshtastic_FromRadio_fields, &msg);
}

} // namespace meshtastic
