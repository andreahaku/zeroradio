/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "icon_button.h"
#include "nav_provider.h"
#include "shell_viewmodel.h"

#include <array>
#include <memory>

namespace app {
class AssetManager;
}

namespace view::widgets {

class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent,
           toolkit::ShellViewModel& shell,
           toolkit::NavProvider& provider,
           app::AssetManager& assets);
    ~NavBar() override;

    void build() override;
    // Make the bar a translucent overlay (content shows through) with white
    // icons, lifted above the content. Call after build().
    void set_overlay_mode();

private:
    // Per-slot context so a single fixed callback can dispatch the right action
    // for the currently-shown tool page (no add/remove churn on page changes).
    struct SlotCtx {
        NavBar* nav{nullptr};
        int     slot{0};
    };

    void create_icon_buttons();
    void update_icon_buttons();
    void apply_overlay_style(); // re-assert the translucent veil after theme re-applies
    void on_slot(int slot);

    static void slot_cb(lv_event_t* event);
    static void update_icons_cb(lv_observer_t* observer, lv_subject_t* subject);

    toolkit::ShellViewModel& shell_;
    toolkit::NavProvider& provider_;
    app::AssetManager& assets_;
    std::array<std::unique_ptr<IconButton>, 5> icon_buttons_;
    std::array<SlotCtx, 5> slot_ctx_{};
    lv_font_t* icon_font_{nullptr};
    lv_font_t* text_font_{nullptr};
    bool overlay_{false};
    lv_observer_t* page_observer_{nullptr};
    lv_observer_t* theme_observer_{nullptr};
};

} // namespace view::widgets
