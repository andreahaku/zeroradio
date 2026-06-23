/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nav_provider.h"
#include "navbar.h"
#include "shell_viewmodel.h"
#include "titlebar.h"

#include "lvgl.h"

#include <memory>

namespace app {
class AssetManager;
}

namespace screen {

class BaseScreen {
public:
    BaseScreen(toolkit::ShellViewModel& shell,
               toolkit::NavProvider& provider,
               app::AssetManager& assets);
    virtual ~BaseScreen();

    BaseScreen(const BaseScreen&) = delete;
    BaseScreen& operator=(const BaseScreen&) = delete;

    void init();
    lv_obj_t* root() const;

protected:
    virtual void build_content(lv_obj_t* content) = 0;

    // Override to hide the TitleBar (the content then claims its 30px). Default
    // keeps the title visible.
    virtual bool show_title_bar() const { return true; }

    // Override to make the NavBar a translucent overlay: the content extends
    // full-height behind it.
    virtual bool overlay_nav_bar() const { return false; }

    toolkit::ShellViewModel& shell();
    toolkit::NavProvider& provider();
    app::AssetManager& assets();

private:
    toolkit::ShellViewModel& shell_;
    toolkit::NavProvider& provider_;
    app::AssetManager& assets_;
    lv_obj_t* root_{nullptr};
    lv_obj_t* content_{nullptr};
    std::unique_ptr<view::widgets::TitleBar> title_bar_;
    std::unique_ptr<view::widgets::NavBar> nav_bar_;
};

} // namespace screen
