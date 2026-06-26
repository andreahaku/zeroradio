/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "app_catalog.h"
#include "shell_viewmodel.h"
#include "subjects.h"

#include "lvgl.h"

#include <string>
#include <vector>

namespace radio {

// State for the Radio hub menu. Derives from the toolkit shell (dark mode, quit,
// title) but — unlike the tool apps — exposes no NavBar, so it is not a
// NavProvider. Holds the selected menu index (a reactive subject the HubScreen
// observes to move the highlight) and the launch target the outer run loop reads
// after the LVGL loop ends.
class HubViewModel : public toolkit::ShellViewModel {
public:
    explicit HubViewModel(const std::vector<AppEntry>& apps);

    int count() const;
    const AppEntry& entry(int index) const;

    // --- selection (observed by the screen for the highlight) ---
    lv_subject_t* selected_subject();
    int selected() const;

    // --- input actions (driven by the screen's key capture) ---
    void move_up();    // wraps to the bottom
    void move_down();  // wraps to the top
    void launch_selected(); // records the target and requests the loop to quit

    // After run_app() returns: the id of the app to launch, or empty to exit the
    // hub (e.g. ESC). resolve_app_binary() turns it into an executable path.
    const std::string& launch_target() const;
    const AppEntry* launch_entry() const;

private:
    const std::vector<AppEntry>& apps_;
    reactive::IntSubject selected_subject_{0};
    std::string launch_target_;
    const AppEntry* launch_entry_{nullptr};
};

} // namespace radio
