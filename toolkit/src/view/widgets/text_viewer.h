/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <functional>
#include <string>
#include <vector>

namespace app {
class AssetManager;
}

namespace view::widgets {

// Appends a small Markdown subset to a flex-column `parent`, one label per line:
// "# " / "## " headings (accent colour), "- " bullets, blank line = spacing,
// **bold** and `code` markers dropped. Enough for the in-app help, the
// changelog and the credits, written one paragraph per line.
void render_markdown(lv_obj_t* parent, const std::string& text, const lv_font_t* heading,
                     const lv_font_t* body, bool dark_mode);

// Full-screen reader for help and About pages. Up/Down (F/X) scroll, Left/Right
// (Z/C) change page when there are several, Esc closes. Captures the keyboard
// while open; `on_close` runs after it released the capture (a screen that
// captures keys itself, like the Radio hub, re-takes it there).
class TextViewer {
public:
    struct Page {
        std::string title;
        std::string markdown;
        // Optional extra content drawn before the markdown (e.g. a QR code).
        std::function<void(lv_obj_t* content)> build;
    };

    TextViewer(app::AssetManager& assets, bool dark_mode, std::vector<Page> pages,
               std::function<void()> on_close = {});
    ~TextViewer();
    TextViewer(const TextViewer&) = delete;
    TextViewer& operator=(const TextViewer&) = delete;

    bool is_open() const { return root_ != nullptr; }

private:
    static void key_cb(uint32_t key, void* ctx);
    void on_key(uint32_t key);
    void show_page(size_t index);
    void close();

    std::vector<Page> pages_;
    std::function<void()> on_close_;
    bool dark_;
    size_t page_ = 0;
    const lv_font_t* heading_font_ = nullptr;
    const lv_font_t* body_font_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* counter_ = nullptr;
    lv_obj_t* content_ = nullptr;
};

} // namespace view::widgets
