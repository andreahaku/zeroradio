/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "entity_store.h"

#include <memory>

namespace ism {

// Feeds decoded ISM readings into an EntityStore on a background thread. The
// mode is chosen from the environment at construction:
//
//   (default)  spawn `rtl_433 -F json` and decode its stdout line by line.
//              ISM_RTL433_ARGS appends extra args (e.g. "-f 868M"); a value of
//              ISM_SOURCE=rtl_tcp:<host>:<port> adds `-d rtl_tcp:<host>:<port>`
//              so a networked dongle can be read (capture-on-host).
//   ISM_JSON=<file>   replay newline-delimited rtl_433 JSON from a file.
//   ISM_SOURCE=mock   synthesise a few drifting readings, no subprocess — the
//                     no-hardware path for UI work.
//
// The live subprocess path is compiled only when ISM_HAVE_SOURCE is defined
// (POSIX); without it, construction falls back to the mock source. The worker
// thread is stopped and joined in the destructor.
class IsmSource {
public:
    explicit IsmSource(toolkit::EntityStore& store);
    ~IsmSource();

    IsmSource(const IsmSource&) = delete;
    IsmSource& operator=(const IsmSource&) = delete;

    void start();
    void stop();

    // True once at least one reading has been decoded (drives the header dot).
    bool ok() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ism
