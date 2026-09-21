/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "shell_viewmodel.h"

#include <string>

namespace toolkit {

// A sibling app binary: next to this executable (the installed package), else
// in the dev build tree (apps/<app_dir>/<config>/<bin>). Empty if not found.
std::string find_sibling(const std::string& bin, const std::string& app_dir);

// If the shell requested a hand-off, exec the sibling app in place of this
// process with the extra environment. Call after run_app() returned (display
// and devices released). Returns only when there is nothing to do (0) or the
// exec failed (1).
int run_handoff(const ShellViewModel& shell);

} // namespace toolkit
