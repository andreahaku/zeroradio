/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "subjects.h"

#include "lvgl.h"

#include <string>
#include <utility>
#include <vector>

namespace toolkit {

// Generic app-shell state, decoupled from any concrete app. Holds the reactive
// subjects the shared widgets (TitleBar, NavBar, BaseScreen) observe, plus a
// pointer to the app's NavProvider (set by the app's viewmodel). An app's
// viewmodel derives from this and also implements NavProvider.
class ShellViewModel {
public:
    ShellViewModel();
    virtual ~ShellViewModel() = default;

    ShellViewModel(const ShellViewModel&) = delete;
    ShellViewModel& operator=(const ShellViewModel&) = delete;

    // --- dark mode ---
    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    // --- lifecycle ---
    void request_quit();

    // Quit, then replace this process with a sibling app (same PID, so a hub
    // waiting on it keeps waiting): main() calls toolkit::run_handoff() once
    // run_app has released the display. `app_dir` finds the binary in the dev
    // build tree (apps/<app_dir>/<config>/<bin>); installed apps sit together.
    struct Handoff {
        std::string bin;
        std::string app_dir;
        std::vector<std::pair<std::string, std::string>> env;
        bool pending() const { return !bin.empty(); }
    };
    void request_handoff(std::string bin, std::string app_dir,
                         std::vector<std::pair<std::string, std::string>> env = {});
    const Handoff& handoff() const { return handoff_; }

    // TAB key: nothing by default; an app overrides it (e.g. SDR <-> Survey).
    virtual void on_tab() {}

    // --- NavBar tool page ---
    void cycle_toolbar();   // slot 0: next tool page (wraps), then bump nav_refresh
    void bump_nav_refresh(); // force a NavBar re-render after slot labels change

    // --- NavProvider wiring ---
    void set_nav_provider(NavProvider* provider);
    int nav_page_count() const; // delegates to the provider (1 if unset)

    // --- subject accessors (raw lv_subject_t* for the shared widgets) ---
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();
    lv_subject_t* toolbar_page_subject();
    lv_subject_t* title_subject();
    lv_subject_t* subtitle_subject();
    lv_subject_t* nav_refresh_subject();

    // --- text helpers (app sets these to drive the bars) ---
    void set_title(const char* text);
    void set_subtitle(const char* text);

private:
    NavProvider* nav_provider_{nullptr};
    Handoff handoff_;

    reactive::BoolSubject       dark_mode_subject_{true};
    reactive::IntSubject        current_page_subject_{0};
    reactive::BoolSubject       quit_requested_subject_{false};
    reactive::IntSubject        toolbar_page_subject_{0};
    reactive::StringSubject<32> title_subject_{""};
    reactive::StringSubject<48> subtitle_subject_{""};
    reactive::IntSubject        nav_refresh_subject_{0};
};

} // namespace toolkit
