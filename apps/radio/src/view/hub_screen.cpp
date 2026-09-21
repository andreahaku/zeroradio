/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "hub_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "docs.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

namespace radio {
namespace {

constexpr int32_t kRowHeight = 36;

} // namespace

HubScreen::HubScreen(HubViewModel& vm, app::AssetManager& assets) : vm_(vm), assets_(assets) {
    build();
}

HubScreen::~HubScreen() {
    // Stop routing keys at this (about-to-be-deleted) screen before run_app's
    // own teardown runs; deleting the root frees the whole object graph.
    platform::set_key_capture(nullptr, nullptr);
    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
}

lv_obj_t* HubScreen::root() const {
    return root_;
}

void HubScreen::build() {
    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(root_, vm_.dark_mode_subject(), reactive::ThemeRole::Screen);

    title_bar_ = std::make_unique<view::widgets::TitleBar>(root_,
                                                          vm_.title_subject(),
                                                          vm_.subtitle_subject(),
                                                          vm_.dark_mode_subject());
    title_bar_->build();

    // Footer key hint, pinned to the bottom.
    constexpr int32_t kFooterHeight = 18;
    auto* footer = lv_label_create(root_);
    lv_label_set_text(footer,
                      LV_SYMBOL_UP " " LV_SYMBOL_DOWN " Select   " LV_SYMBOL_OK
                                   " Open   " LV_SYMBOL_CLOSE " Exit");
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_opa(footer, LV_OPA_60, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -3);
    reactive::bind_theme(footer, vm_.dark_mode_subject(), reactive::ThemeRole::Text);

    // The app list fills the band between the title bar and the footer.
    auto* list = lv_obj_create(root_);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, view::kScreenWidth,
                    view::kScreenHeight - view::kTitleBarHeight - kFooterHeight);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, view::kTitleBarHeight);
    // Vertically scrollable so the menu holds more apps than fit at once (the
    // selected row is scrolled into view in apply_highlight); scrollbar hidden.
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    // Top-anchored (not centred) on the main axis: centring fights scroll_to_view
    // when the list overflows, leaving the last row off-screen yet selectable.
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_hor(list, 8, 0);
    lv_obj_set_style_pad_ver(list, 4, 0);

    auto* icon_font = assets_.load_font("Phosphor-Fill.ttf", 22);
    auto* name_font = assets_.load_font("inter-semibold.ttf", 16);
    auto* sub_font = assets_.load_font("inter-regular.ttf", 11);

    rows_.reserve(static_cast<size_t>(vm_.count()));
    for (int i = 0; i < vm_.count(); ++i) {
        rows_.push_back(build_row(list, vm_.entry(i), icon_font, name_font, sub_font));
    }

    apply_highlight();
    reactive::observe_obj(root_, vm_.selected_subject(), selected_cb, this);

    // Keyboard-only navigation: grab every key so the digits/arrows drive the
    // menu instead of the (absent) NavBar. ESC must be handled here too, since
    // capture bypasses run_app's ESC->quit routing.
    platform::set_key_capture(key_cb, this);
}

lv_obj_t* HubScreen::build_row(lv_obj_t* parent, const AppEntry& entry, lv_font_t* icon_font,
                               lv_font_t* name_font, lv_font_t* sub_font) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kRowHeight);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_radius(row, 6, 0);

    auto* icon = lv_label_create(row);
    lv_label_set_text(icon, entry.icon);
    if (icon_font) {
        lv_obj_set_style_text_font(icon, icon_font, 0);
    }

    auto* text_col = lv_obj_create(row);
    lv_obj_remove_style_all(text_col);
    lv_obj_set_size(text_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text_col, 1);
    lv_obj_clear_flag(text_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text_col, 1, 0);

    auto* name = lv_label_create(text_col);
    lv_label_set_text(name, entry.name);
    lv_obj_set_style_text_font(name, name_font ? name_font : &lv_font_montserrat_14, 0);

    auto* subtitle = lv_label_create(text_col);
    lv_label_set_text(subtitle, entry.subtitle);
    lv_obj_set_style_text_font(subtitle, sub_font ? sub_font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_opa(subtitle, LV_OPA_70, 0);

    return row;
}

void HubScreen::apply_highlight() {
    const view::ThemePalette pal = view::palette(vm_.is_dark_mode());
    const int selected = vm_.selected();

    for (size_t i = 0; i < rows_.size(); ++i) {
        lv_obj_t* row = rows_[i];
        const bool active = static_cast<int>(i) == selected;
        if (active) {
            lv_obj_set_style_bg_color(row, pal.primary, 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(row, pal.background, 0);
        } else {
            lv_obj_set_style_bg_color(row, pal.surface, 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_30, 0);
            lv_obj_set_style_text_color(row, pal.text, 0);
        }
    }

    // Keep the highlighted row visible when the menu has more apps than fit.
    if (selected >= 0 && selected < static_cast<int>(rows_.size())) {
        lv_obj_scroll_to_view(rows_[static_cast<size_t>(selected)], LV_ANIM_OFF);
    }
}

void HubScreen::selected_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* self = static_cast<HubScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->apply_highlight();
    }
}

void HubScreen::key_cb(uint32_t key, void* ctx) {
    auto* self = static_cast<HubScreen*>(ctx);
    if (!self) {
        return;
    }

    switch (key) {
        case LV_KEY_UP:
        case '5': // CardputerZero nav key reused as "up"
            self->vm_.move_up();
            break;
        case LV_KEY_DOWN:
        case '7': // ...and "down"
            self->vm_.move_down();
            break;
        case LV_KEY_ENTER:
        case LV_KEY_RIGHT:
        case '6': // ...and "open"
            if (std::string(self->vm_.entry(self->vm_.selected()).id) == "about") {
                self->open_about();
            } else {
                self->vm_.launch_selected();
            }
            break;
        case LV_KEY_ESC:
            self->vm_.request_quit();
            break;
        default:
            break;
    }
}

#ifndef ZERORADIO_REPO_URL
#define ZERORADIO_REPO_URL "https://github.com/andreahaku/zeroradio"
#endif

void HubScreen::open_about() {
    using Page = view::widgets::TextViewer::Page;
    const std::string url = ZERORADIO_REPO_URL;
    std::string short_url = url;
    if (short_url.rfind("https://", 0) == 0) short_url.erase(0, 8);

    Page info;
    info.title = "About";
    info.markdown = "# ZeroRadio " APP_VERSION "\n"
                    "Radio tools for the M5Stack CardputerZero: SDR, spectrum survey, ISM, ADS-B and AIS.\n"
                    "## Developer\n"
                    "Andrea Salvatore (IU4APC)\n"
                    "## Source code\n" +
                    short_url + "\n"
                    "Scan the QR code with your phone.\n"
                    "## License\n"
                    "MIT. The bundled decoders are GPL-3.0: see Credits.";
    info.build = [url](lv_obj_t* content) {
        // Dark modules on a white quiet zone: phone cameras need the contrast.
        auto* qr = lv_qrcode_create(content);
        lv_qrcode_set_size(qr, 96);
        lv_qrcode_set_dark_color(qr, lv_color_black());
        lv_qrcode_set_light_color(qr, lv_color_white());
        lv_qrcode_update(qr, url.c_str(), static_cast<uint32_t>(url.size()));
        lv_obj_set_style_border_color(qr, lv_color_white(), 0);
        lv_obj_set_style_border_width(qr, 4, 0);
    };

    std::string changelog = toolkit::read_doc("CHANGELOG.md");
    std::string credits = toolkit::read_doc("CREDITS.md");
    if (changelog.empty()) changelog = "CHANGELOG.md is not installed.";
    if (credits.empty()) credits = "CREDITS.md is not installed.";

    about_ = std::make_unique<view::widgets::TextViewer>(
        assets_, vm_.is_dark_mode(),
        std::vector<Page>{info, {"Changelog", changelog, {}}, {"Credits", credits, {}}},
        [this]() { platform::set_key_capture(key_cb, this); }); // back to the menu keys
}

} // namespace radio
