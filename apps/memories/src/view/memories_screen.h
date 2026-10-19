/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "memories_viewmodel.h"

#include <string>

namespace memories {

class MemoriesScreen : public screen::BaseScreen {
public:
    MemoriesScreen(MemoriesViewModel& vm, app::AssetManager& assets);
    ~MemoriesScreen() override;

protected:
    void build_content(lv_obj_t* content) override;
    bool show_title_bar() const override { return false; }

private:
    static void rows_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void search_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void draw_cb(lv_event_t* event);
    static void search_key_cb(uint32_t key, void* ctx);
    void update_table();
    void open_search();
    void close_search(bool apply);
    void on_search_key(uint32_t key);
    void render_search();

    MemoriesViewModel& vm_;
    lv_obj_t* table_ = nullptr;
    lv_obj_t* empty_label_ = nullptr;
    lv_obj_t* search_dialog_ = nullptr;
    lv_obj_t* search_input_ = nullptr;
    lv_observer_t* rows_observer_ = nullptr;
    lv_observer_t* selection_observer_ = nullptr;
    lv_observer_t* search_observer_ = nullptr;
    int last_search_request_ = 0;
    std::string search_text_;
};

} // namespace memories
