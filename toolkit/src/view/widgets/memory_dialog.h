/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <functional>
#include <string>

namespace app {
class AssetManager;
}

namespace toolkit {

// Full-screen modal to name a frequency memory before it is saved (or to
// rename one): a name and a group, both prefilled. Typing replaces a field's
// prefilled text, Backspace edits it. Enter moves from the name to the group,
// then saves; Tab or Up/Down switch fields; Esc cancels. Captures the keyboard
// as text while open; the object can be destroyed at any time.
class MemoryDialog {
public:
    using OnSave = std::function<void(const std::string& name, const std::string& group)>;

    // `title` heads the dialog ("Save memory"), `info` describes the channel
    // ("88.700 MHz  WFM").
    MemoryDialog(app::AssetManager& assets, bool dark_mode, const std::string& title,
                 const std::string& info, std::string name, std::string group, OnSave on_save);
    ~MemoryDialog();
    MemoryDialog(const MemoryDialog&) = delete;
    MemoryDialog& operator=(const MemoryDialog&) = delete;

    // False once the user saved or cancelled; the owner may then drop it.
    bool is_open() const { return root_ != nullptr; }

    // Field limits: the Memories list column fits about 20 characters.
    static constexpr size_t kMaxName = 24;
    static constexpr size_t kMaxGroup = 16;

private:
    struct Field {
        std::string text;
        bool fresh = true; // still the prefilled text: typing replaces it
        size_t max = 0;
        lv_obj_t* label = nullptr;
    };

    static void key_cb(uint32_t key, void* ctx);
    void on_key(uint32_t key);
    void render();
    void close();

    OnSave on_save_;
    bool dark_;
    Field fields_[2]; // name, group
    int active_ = 0;
    lv_obj_t* root_ = nullptr;
};

} // namespace toolkit
