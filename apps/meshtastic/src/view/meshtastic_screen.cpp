/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "asset_manager.h"
#include "bindings.h"

namespace meshtastic {
namespace {

// Light poll: just enough to notice a key-4 page cycle. No data work yet.
constexpr uint32_t kTickPeriodMs = 150;

} // namespace

MeshtasticScreen::MeshtasticScreen(MeshtasticViewModel& vm, app::AssetManager& assets)
    : BaseScreen(vm, vm, assets), vm_(vm) {
    init();
}

MeshtasticScreen::~MeshtasticScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void MeshtasticScreen::build_content(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 4, 0);

    font_big_   = assets().load_font("inter-semibold.ttf", 22);
    font_small_ = assets().load_font("inter-regular.ttf", 12);

    view_label_ = lv_label_create(content);
    lv_label_set_text(view_label_, vm_.page_name(static_cast<int>(MeshtasticViewModel::Page::Chats)));
    lv_obj_set_style_text_font(view_label_, font_big_ ? font_big_ : &lv_font_montserrat_12, 0);
    reactive::bind_theme(view_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);

    hint_label_ = lv_label_create(content);
    lv_label_set_text(hint_label_, "press 4 to switch view  -  ESC to quit");
    lv_obj_set_style_text_font(hint_label_, font_small_ ? font_small_ : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint_label_, lv_color_hex(0x888888), 0);

    last_page_ = -1;
    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);
    tick(); // initial paint
}

void MeshtasticScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<MeshtasticScreen*>(lv_timer_get_user_data(timer));
    if (self) {
        self->tick();
    }
}

void MeshtasticScreen::tick() {
    const int page = vm_.page();
    if (page == last_page_) {
        return;
    }
    last_page_ = page;
    if (view_label_) {
        lv_label_set_text(view_label_, vm_.page_name(page));
    }
    vm_.set_subtitle(vm_.page_name(page)); // TitleBar shows "MESH" + the view name
}

} // namespace meshtastic
