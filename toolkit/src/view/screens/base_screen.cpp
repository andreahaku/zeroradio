/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"

namespace screen {

BaseScreen::BaseScreen(toolkit::ShellViewModel& shell,
                       toolkit::NavProvider& provider,
                       app::AssetManager& assets)
    : shell_(shell), provider_(provider), assets_(assets) {}

BaseScreen::~BaseScreen() {
    title_bar_.reset();
    nav_bar_.reset();

    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
}

void BaseScreen::init() {
    if (root_) {
        return;
    }

    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(root_, shell_.dark_mode_subject(), reactive::ThemeRole::Screen);

    const int32_t top_offset = show_title_bar() ? view::kTitleBarHeight : 0;
    if (show_title_bar()) {
        title_bar_ = std::make_unique<view::widgets::TitleBar>(root_,
                                                              shell_.title_subject(),
                                                              shell_.subtitle_subject(),
                                                              shell_.dark_mode_subject());
        title_bar_->build();
    }

    nav_bar_ = std::make_unique<view::widgets::NavBar>(root_, shell_, provider_, assets_);
    nav_bar_->build();

    // When the NavBar is an overlay, the content claims its 30px too and the bar
    // floats on top (handled below); otherwise the content stops above it.
    const int32_t bottom_reserved = overlay_nav_bar() ? 0 : view::kNavBarHeight;

    content_ = lv_obj_create(root_);
    lv_obj_remove_style_all(content_);
    lv_obj_set_size(content_, LV_PCT(100), view::kScreenHeight - top_offset - bottom_reserved);
    lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, top_offset);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(content_, shell_.dark_mode_subject(), reactive::ThemeRole::Surface);

    build_content(content_);

    if (overlay_nav_bar()) {
        nav_bar_->set_overlay_mode();
    }
}

lv_obj_t* BaseScreen::root() const {
    return root_;
}

toolkit::ShellViewModel& BaseScreen::shell() {
    return shell_;
}

toolkit::NavProvider& BaseScreen::provider() {
    return provider_;
}

app::AssetManager& BaseScreen::assets() {
    return assets_;
}

} // namespace screen
