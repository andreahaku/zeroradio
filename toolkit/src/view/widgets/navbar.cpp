/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <cstdio>

namespace view::widgets {
namespace {

constexpr std::array<int32_t, 5> kNavButtonXOffsets = {36, 17, 2, -17, -36};

// Page number shown on slot 0 (1-based).
constexpr std::array<const char*, 5> kPageLabels = {"1", "2", "3", "4", "5"};

} // namespace

NavBar::NavBar(lv_obj_t* parent,
               toolkit::ShellViewModel& shell,
               toolkit::NavProvider& provider,
               app::AssetManager& assets)
    : BaseWidgets(parent), shell_(shell), provider_(provider), assets_(assets) {}

NavBar::~NavBar() {
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (icon_buttons_[i]) {
            platform::unregister_nav_button(i, icon_buttons_[i]->root());
        }
    }

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), view::kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(core_obj_, 8, 0);
    lv_obj_set_style_pad_right(core_obj_, 8, 0);
    reactive::bind_theme(core_obj_, shell_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_icon_buttons();
    // The icon set follows the tool page; theme changes recolour the icons; the
    // nav_refresh bump forces a re-render when app-defined slot labels change.
    // The last observer is auto-removed with core_obj_, so it is not stored.
    page_observer_ = reactive::observe_obj(core_obj_, shell_.toolbar_page_subject(), update_icons_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, shell_.dark_mode_subject(), update_icons_cb, this);
    reactive::observe_obj(core_obj_, shell_.nav_refresh_subject(), update_icons_cb, this);
    update_icon_buttons();
}

void NavBar::apply_overlay_style() {
    if (!overlay_ || !core_obj_) {
        return;
    }
    // Translucent dark veil so the content stays visible through the bar, minus
    // the theme's 1px border. Re-applied after any theme refresh, which would
    // otherwise restore the opaque Bar background.
    lv_obj_set_style_bg_color(core_obj_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_40, 0);
    lv_obj_set_style_border_width(core_obj_, 0, 0);
}

void NavBar::set_overlay_mode() {
    if (!core_obj_) {
        return;
    }
    overlay_ = true;
    apply_overlay_style();
    // Lift the bar above the (full-height) content...
    lv_obj_move_foreground(core_obj_);
    // ...with white icons for contrast against the content colours.
    for (auto& button : icon_buttons_) {
        if (button) {
            button->set_icon_color(lv_color_white());
        }
    }
}

void NavBar::create_icon_buttons() {
    const auto light_color = view::palette(false).text;
    const auto dark_color = view::palette(true).text;
    icon_font_ = assets_.load_font("Phosphor-Fill.ttf", 26);
    text_font_ = assets_.load_font("inter-semibold.ttf", 16);

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        slot_ctx_[i] = SlotCtx{this, static_cast<int>(i)};
        icon_buttons_[i] = std::make_unique<IconButton>(core_obj_,
                                                        shell_.dark_mode_subject(),
                                                        32,
                                                        22,
                                                        "",
                                                        icon_font_ ? icon_font_ : &lv_font_montserrat_14,
                                                        light_color,
                                                        dark_color,
                                                        slot_cb,
                                                        &slot_ctx_[i]);
        icon_buttons_[i]->build();
        lv_obj_set_style_translate_x(icon_buttons_[i]->root(), kNavButtonXOffsets[i], 0);
        platform::register_nav_button(i, icon_buttons_[i]->root());
    }
}

void NavBar::update_icon_buttons() {
    const int page = lv_subject_get_int(shell_.toolbar_page_subject());

    // Slots 1..4 come from the app's NavProvider; slot 0 is the page switcher.
    toolkit::NavProvider::NavSlot slots[5] = {};
    provider_.nav_fill(page, slots);

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        auto& button = icon_buttons_[i];
        if (!button || !button->root()) {
            continue;
        }

        const char* text;
        bool use_text_font;
        bool enabled;
        if (i == 0) {
            // Slot 0 always shows the 1-based page number, rendered with the text
            // font and always enabled (it is the page switcher).
            text = kPageLabels[page >= 0 && page < 5 ? page : 0];
            use_text_font = true;
            enabled = true;
        } else {
            const auto& slot = slots[i];
            text = slot.text;
            use_text_font = slot.text_font;
            enabled = slot.enabled;
        }

        // Every key keeps a visible button so the 5 slots stay aligned with the
        // Cardputer's physical keys; empty slots show a disabled placeholder.
        const bool placeholder = (text == nullptr || text[0] == '\0');
        button->set_font((use_text_font || placeholder) ? text_font_ : icon_font_);
        button->set_text(placeholder ? "\xC2\xB7" : text); // middot placeholder
        button->set_enabled((i == 0) ? true : (!placeholder && enabled));
        if (overlay_) {
            button->set_icon_color(lv_color_white());
        }
    }

    // The theme observer (bound to the same dark-mode subject) may have just
    // restored the opaque Bar background; re-assert the overlay veil on top.
    apply_overlay_style();
}

void NavBar::on_slot(int slot) {
    // Slot 0 is the page switcher on every page; the shell owns that action.
    if (slot == 0) {
        shell_.cycle_toolbar();
        return;
    }

    const int page = lv_subject_get_int(shell_.toolbar_page_subject());
    provider_.nav_activate(page, slot);
}

void NavBar::slot_cb(lv_event_t* event) {
    auto* ctx = static_cast<SlotCtx*>(lv_event_get_user_data(event));
    if (ctx && ctx->nav) {
        ctx->nav->on_slot(ctx->slot);
    }
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);

    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_icon_buttons();
    }
}

} // namespace view::widgets
