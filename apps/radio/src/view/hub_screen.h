/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hub_viewmodel.h"
#include "text_viewer.h"
#include "titlebar.h"

#include "lvgl.h"

#include <memory>
#include <vector>

namespace app {
class AssetManager;
}

namespace radio {

// The Radio launcher screen: a TitleBar plus a vertical list of apps with a
// moving highlight and a footer key hint. Navigation is keyboard-driven via the
// platform key-capture hook (UP/DOWN + ENTER, ESC to exit), so there is no
// NavBar. Standalone (not a toolkit BaseScreen) to keep the launcher chrome
// minimal.
class HubScreen {
public:
    HubScreen(HubViewModel& vm, app::AssetManager& assets);
    ~HubScreen();

    HubScreen(const HubScreen&) = delete;
    HubScreen& operator=(const HubScreen&) = delete;

    lv_obj_t* root() const;

private:
    void build();
    lv_obj_t* build_row(lv_obj_t* parent, const AppEntry& entry, lv_font_t* icon_font,
                        lv_font_t* name_font, lv_font_t* sub_font);
    void apply_highlight();

    static void selected_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void key_cb(uint32_t key, void* ctx);
    void open_about();

    HubViewModel& vm_;
    app::AssetManager& assets_;
    lv_obj_t* root_{nullptr};
    std::unique_ptr<view::widgets::TitleBar> title_bar_;
    std::unique_ptr<view::widgets::TextViewer> about_;
    std::vector<lv_obj_t*> rows_;
};

} // namespace radio
