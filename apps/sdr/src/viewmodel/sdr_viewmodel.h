/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "shell_viewmodel.h"

namespace sdr {

// SDR app state. Derives from the toolkit shell and implements NavProvider to
// drive the 5-key bar, mirroring AdsbViewModel. This is the scaffold: the real
// SDR state (VFO, mode, span, gain, S-meter) and the 5 toolbar pages
// (tuning/zoom/visual/audio/settings) are ported in later steps from
// SDRTerminal's BaseViewModel. See docs/sdrterminal-port-plan.md.
class SdrViewModel : public toolkit::ShellViewModel, public toolkit::NavProvider {
public:
    SdrViewModel();

    // --- NavProvider ---
    int nav_page_count() const override;
    void nav_fill(int page, NavProvider::NavSlot out[5]) const override;
    void nav_activate(int page, int slot) override;
};

} // namespace sdr
