/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace toolkit {

// Text shipped with the suite (help pages, CHANGELOG.md, CREDITS.md), by its
// path relative to the repository root, e.g. "docs/help/sdr.md". The package
// installs these files under /usr/share/zeroradio with the same relative paths;
// a desktop build reads them from the source tree. Empty if missing.
std::string read_doc(const std::string& relative_path);

} // namespace toolkit
