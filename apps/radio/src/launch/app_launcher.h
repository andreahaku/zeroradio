/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "app_catalog.h"

#include <string>

namespace radio {

// Resolve `entry`'s executable to an absolute path, trying (in order):
//   1. $RADIO_APPS_DIR/<bin>                      (explicit dev override)
//   2. <hub_dir>/<bin>                            (install: all colocated)
//   3. <hub_dir>/../<app_dir>/Debug|Release/<bin> (dev CMake build tree)
// where <hub_dir> is the directory of the running hub (from /proc/self/exe).
// Returns an empty string if no candidate exists on disk.
std::string resolve_app_binary(const AppEntry& entry);

// Spawn `path` as a child process, inheriting the current environment (so
// REMOTE_FB / ADSB_JSON / etc. propagate), and block until it exits. The hub
// must have already released the display before calling this. Returns the
// child's exit code, or -1 if the spawn failed.
int run_app_binary(const std::string& path);

} // namespace radio
