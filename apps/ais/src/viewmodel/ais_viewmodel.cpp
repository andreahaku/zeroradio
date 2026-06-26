/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_viewmodel.h"

namespace ais {

AisViewModel::AisViewModel() {
    set_nav_provider(this);
    set_title("AIS");
}

int AisViewModel::nav_page_count() const {
    return 1;
}

void AisViewModel::nav_fill(int /*page*/, NavProvider::NavSlot /*out*/[5]) const {
    // First cut: a single static list view, no per-slot actions. ESC quits
    // (handled by run_app). Slots stay as placeholders.
}

void AisViewModel::nav_activate(int /*page*/, int /*slot*/) {}

} // namespace ais
