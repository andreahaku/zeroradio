/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "memories_viewmodel.h"
#include "memory_dialog.h"

#include <memory>
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
    static void scan_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void edit_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void tick_cb(lv_timer_t* timer);
    static void draw_cb(lv_event_t* event);
    static void search_key_cb(uint32_t key, void* ctx);
    static void scan_key_cb(uint32_t key, void* ctx);
    void update_table();
    void open_search();
    void close_search(bool apply);
    void on_search_key(uint32_t key);
    void render_search();
    void open_scan();
    void close_scan(bool confirm);
    void on_scan_key(uint32_t key);

    MemoriesViewModel& vm_;
    lv_obj_t* table_ = nullptr;
    lv_obj_t* empty_label_ = nullptr;
    lv_obj_t* search_dialog_ = nullptr;
    lv_obj_t* search_input_ = nullptr;
    lv_obj_t* scan_dialog_ = nullptr;
    lv_observer_t* rows_observer_ = nullptr;
    lv_observer_t* selection_observer_ = nullptr;
    lv_observer_t* search_observer_ = nullptr;
    lv_observer_t* scan_observer_ = nullptr;
    lv_observer_t* edit_observer_ = nullptr;
    lv_timer_t* tick_timer_ = nullptr; // expires the EDIT delete confirmation
    int last_edit_request_ = 0;
    std::unique_ptr<toolkit::MemoryDialog> edit_dialog_; // EDIT page key 5
    int last_search_request_ = 0;
    int last_scan_request_ = 0;
    std::string search_text_;
};

} // namespace memories
