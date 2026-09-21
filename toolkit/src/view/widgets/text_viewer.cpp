/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "text_viewer.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

#include <sstream>

namespace view::widgets {

namespace {

constexpr int32_t kScrollStep = 28;

// Drops the inline markers the small screen can't style: **bold** and `code`.
std::string strip_inline(std::string s) {
    for (const char* m : {"**", "`"}) {
        for (size_t at = s.find(m); at != std::string::npos; at = s.find(m, at)) {
            s.erase(at, std::char_traits<char>::length(m));
        }
    }
    return s;
}

} // namespace

void render_markdown(lv_obj_t* parent, const std::string& text, const lv_font_t* heading,
                     const lv_font_t* body, bool dark_mode) {
    const auto pal = view::palette(dark_mode);
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            auto* gap = lv_obj_create(parent);
            lv_obj_remove_style_all(gap);
            lv_obj_set_size(gap, 1, 4);
            continue;
        }
        const bool h1 = line.rfind("# ", 0) == 0;
        const bool h2 = line.rfind("## ", 0) == 0 || line.rfind("### ", 0) == 0;
        const bool bullet = line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0;
        std::string shown;
        if (h1 || h2) {
            shown = line.substr(line.find(' ') + 1);
        } else if (bullet) {
            shown = "\xE2\x80\xA2 " + line.substr(2); // "• "
        } else {
            shown = line;
        }

        auto* label = lv_label_create(parent);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(label, strip_inline(shown).c_str());
        lv_obj_set_style_text_font(label, (h1 || h2) ? heading : body, 0);
        lv_obj_set_style_text_color(label, (h1 || h2) ? pal.primary : pal.text, 0);
        if (bullet) lv_obj_set_style_pad_left(label, 6, 0);
        if (h1 || h2) lv_obj_set_style_pad_top(label, 2, 0);
    }
}

TextViewer::TextViewer(app::AssetManager& assets, bool dark_mode, std::vector<Page> pages,
                       std::function<void()> on_close)
    : pages_(std::move(pages)), on_close_(std::move(on_close)), dark_(dark_mode) {
    if (pages_.empty()) pages_.push_back({"", "", {}});
    const lv_font_t* fallback = &lv_font_montserrat_12;
    heading_font_ = assets.load_font("inter-semibold.ttf", 14);
    body_font_ = assets.load_font("inter-regular.ttf", 12);
    if (!heading_font_) heading_font_ = fallback;
    if (!body_font_) body_font_ = fallback;
    const auto pal = view::palette(dark_);

    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(root_, pal.background, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 6, 0);
    lv_obj_set_style_pad_row(root_, 3, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);

    auto* header = lv_obj_create(root_);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    title_ = lv_label_create(header);
    lv_obj_set_style_text_font(title_, heading_font_, 0);
    lv_obj_set_style_text_color(title_, pal.primary, 0);
    lv_obj_align(title_, LV_ALIGN_LEFT_MID, 0, 0);
    counter_ = lv_label_create(header);
    lv_obj_set_style_text_font(counter_, body_font_, 0);
    lv_obj_set_style_text_color(counter_, pal.text_disabled, 0);
    lv_obj_align(counter_, LV_ALIGN_RIGHT_MID, 0, 0);

    content_ = lv_obj_create(root_);
    lv_obj_remove_style_all(content_);
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content_, 1, 0);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_AUTO);

    auto* hint = lv_label_create(root_);
    lv_label_set_text(hint, pages_.size() > 1 ? "F/X scroll   Z/C page   Esc close"
                                              : "F/X scroll   Esc close");
    lv_obj_set_style_text_font(hint, body_font_, 0);
    lv_obj_set_style_text_color(hint, pal.text_disabled, 0);

    show_page(0);
    platform::set_key_capture(key_cb, this);
}

TextViewer::~TextViewer() {
    if (root_) {
        // Destroyed while open (e.g. the app quits): release without on_close.
        platform::set_key_capture(nullptr, nullptr);
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void TextViewer::key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<TextViewer*>(ctx)) self->on_key(key);
}

void TextViewer::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
            lv_obj_scroll_by_bounded(content_, 0, kScrollStep, LV_ANIM_OFF);
            break;
        case LV_KEY_DOWN:
            lv_obj_scroll_by_bounded(content_, 0, -kScrollStep, LV_ANIM_OFF);
            break;
        case LV_KEY_LEFT:
            if (page_ > 0) show_page(page_ - 1);
            break;
        case LV_KEY_RIGHT:
            if (page_ + 1 < pages_.size()) show_page(page_ + 1);
            break;
        case LV_KEY_ESC:
            close();
            break;
        default:
            break;
    }
}

void TextViewer::show_page(size_t index) {
    page_ = index;
    const auto& page = pages_[page_];
    lv_label_set_text(title_, page.title.c_str());
    if (pages_.size() > 1) {
        lv_label_set_text_fmt(counter_, "%u/%u", static_cast<unsigned>(page_ + 1),
                              static_cast<unsigned>(pages_.size()));
    } else {
        lv_label_set_text(counter_, "");
    }
    lv_obj_clean(content_);
    if (page.build) page.build(content_);
    render_markdown(content_, page.markdown, heading_font_, body_font_, dark_);
    lv_obj_scroll_to_y(content_, 0, LV_ANIM_OFF);
}

void TextViewer::close() {
    if (!root_) return;
    platform::set_key_capture(nullptr, nullptr);
    lv_obj_delete(root_);
    root_ = nullptr;
    if (on_close_) on_close_();
}

} // namespace view::widgets
