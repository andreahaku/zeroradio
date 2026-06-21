/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <filesystem>
#include <string>

namespace toolkit {

// Reusable per-app config-location helpers, shared by any app that persists a
// small session blob (e.g. the SDR VFO/mode, an app's last view). The on-disk
// format is the app's business; the toolkit only resolves where it lives.

// Directory for an app's state: "$XDG_CONFIG_HOME/<app_subdir>" or
// "$HOME/.config/<app_subdir>". Returns an empty path if neither env var is set.
std::filesystem::path config_dir(const std::string& app_subdir);

// config_dir(app_subdir) / filename, or an empty path if config_dir is empty.
std::filesystem::path config_file(const std::string& app_subdir, const std::string& filename);

// Create the parent directory of `path` if needed. Returns false on failure (or
// if the path is empty). Safe to call before writing a state file.
bool ensure_parent_dir(const std::filesystem::path& path);

} // namespace toolkit
