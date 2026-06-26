/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "shell_viewmodel.h"

namespace ais {

// Minimal AIS app state: the toolkit shell (title, dark mode, quit) plus a
// trivial NavProvider so the shared NavBar renders. The first cut shows a vessel
// list; the radar/Mercator map port (reusing apps/adsb's render_scope) lands
// later — see apps/ais/README.md.
class AisViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    AisViewModel();

    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;
};

} // namespace ais
