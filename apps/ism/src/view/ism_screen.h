/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "entity_store.h"
#include "ism_viewmodel.h"

#include "lvgl.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ism {

// The ISM sniffer screen: a header (app name + "N dev" + source dot) over a
// body that switches between List / Detail / Settings by the viewmodel's
// toolbar page. A ~400 ms timer snapshots the EntityStore, sorts it, and
// refreshes the active view. TitleBar hidden; a solid NavBar drives the keys.
class IsmScreen : public screen::BaseScreen {
public:
    IsmScreen(IsmViewModel& vm, app::AssetManager& assets, toolkit::EntityStore& store,
              std::function<bool()> source_state);
    ~IsmScreen() override;

protected:
    void build_content(lv_obj_t* content) override;
    bool show_title_bar() const override { return false; }
    bool overlay_nav_bar() const override { return false; }

private:
    static constexpr int32_t kHeaderHeight = 18;
    static constexpr uint32_t kTickPeriodMs = 400;

    // A sorted view row built from an EntityStore snapshot each tick.
    struct Row {
        std::string key;      // device_key (stable id)
        std::string model;
        std::string type;
        double age{0.0};      // seconds since last heard
        bool has_rssi{false};
        double rssi{0.0};
        const toolkit::Entity* entity{nullptr}; // for the Detail field dump
    };

    static void tick_cb(lv_timer_t* timer);
    void tick();
    static void list_draw_event_cb(lv_event_t* event);
    static void settings_draw_event_cb(lv_event_t* event);

    std::vector<Row> build_rows(const std::vector<toolkit::Entity>& snap) const;
    void update_list(const std::vector<Row>& rows);
    void update_detail(const std::vector<Row>& rows);
    void update_settings();
    void show_view(int screen);
    int row_of(const std::vector<Row>& rows, const std::string& key) const;

    IsmViewModel& vm_;
    toolkit::EntityStore& store_;
    std::function<bool()> source_state_;

    lv_obj_t* header_       = nullptr;
    lv_obj_t* header_count_ = nullptr;
    lv_obj_t* header_title_ = nullptr;
    lv_obj_t* conn_dot_     = nullptr;

    lv_obj_t* body_         = nullptr;
    lv_obj_t* list_view_    = nullptr;
    lv_obj_t* list_table_   = nullptr;
    lv_obj_t* detail_box_   = nullptr;
    lv_obj_t* detail_label_ = nullptr;
    lv_obj_t* settings_box_   = nullptr;
    lv_obj_t* settings_table_ = nullptr;

    int list_sel_row_ = -1;
    int settings_sel_row_ = -1;

    lv_timer_t* timer_ = nullptr;

    const lv_font_t* font_small_ = nullptr;
    const lv_font_t* font_mono_  = nullptr;
    const lv_font_t* font_bold_  = nullptr;

    std::vector<toolkit::Entity> snapshot_; // owns entities the rows point into
};

} // namespace ism
