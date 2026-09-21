/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "app_catalog.h"
#include "app_launcher.h"
#include "asset_manager.h"
#include "hub_screen.h"
#include "hub_viewmodel.h"
#include "logger.h"
#include "run_app.h"

#include "lvgl.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

// Re-launch the hub as a brand-new process. LVGL + the SDL backend keep static
// state that is not cleanly re-initialisable in-process (a second lv_init()
// leaves the SDL event pump dead -> a frozen, unresponsive window), so rather
// than loop in-process we exec a fresh image for every menu render. State is
// intentionally not carried over (the selection resets to the top).
[[noreturn]] void reexec_hub(char** argv) {
    std::fflush(nullptr); // execv discards stdio buffers; flush pending logs first
    execv("/proc/self/exe", argv);
    // execv only returns on failure.
    LOG_ERROR("radio hub: re-exec failed; exiting");
    _exit(1);
}

} // namespace

// The Radio hub: a thin launcher (Option A1). It renders the menu in one LVGL
// session, hands the single display off to the chosen app's binary (a separate
// process) and, when that app exits, re-execs itself to show the menu again.
// ESC on the menu exits the hub back to the system launcher.
//
// The display hand-off is the whole point: run_app() (given a teardown callback)
// releases the framebuffer/port after the loop — deleting the screen, the
// indevs, the remote-fb server and the display — so the spawned child can claim
// it. The parent then blocks in waitpid() holding no display at all.
int main(int /*argc*/, char** argv) {
    std::string target_id;
    const radio::AppEntry* target_entry = nullptr;

    // Only list apps whose binary is actually present, so a package that ships
    // a subset of the suite (e.g. without Meshtastic) shows no dead entries.
    // Outlives the menu scope: the view model and target_entry point into it.
    std::vector<radio::AppEntry> installed;
    for (const auto& entry : radio::app_catalog()) {
        if (!radio::resolve_app_binary(entry).empty()) installed.push_back(entry);
    }
    // Last row: About opens in the hub itself (developer, changelog, credits).
    installed.push_back(radio::about_entry());

    {
        app::AssetManager assets;
        radio::HubViewModel view_model(installed);
        std::unique_ptr<radio::HubScreen> screen;

        toolkit::run_app(
            view_model, assets,
            [&]() -> lv_obj_t* {
                screen = std::make_unique<radio::HubScreen>(view_model, assets);
                return screen->root();
            },
            [&]() {
                // Runs while the display is still alive: capture the choice, then
                // delete the screen graph before run_app drops the display.
                target_id = view_model.launch_target();
                target_entry = view_model.launch_entry();
                screen.reset();
            });
    } // AssetManager destroyed here (freetype fonts freed, LVGL still up)

    if (target_id.empty() || !target_entry) {
        return 0; // ESC on the menu -> leave the hub
    }

    const std::string path = radio::resolve_app_binary(*target_entry);
    if (path.empty()) {
        LOG_ERROR("radio hub: could not locate binary for '{}'", target_id);
    } else {
        LOG_INFO("radio hub: launching {} ({})", target_id, path);
        std::fflush(nullptr); // make the launch visible before the child takes over
        // Blocks until the app exits (display is free). An app left with a 3 s
        // ESC hold asks for the system launcher: close the hub too.
        if (radio::run_app_binary(path) == toolkit::ShellViewModel::kExitHome) return 0;
    }

    reexec_hub(argv); // fresh process -> pristine LVGL/SDL -> show the menu again
}
