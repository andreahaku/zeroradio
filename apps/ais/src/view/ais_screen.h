/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ais_viewmodel.h"
#include "app_config.h"
#include "base_screen.h"
#include "entity_store.h"
#include "location_dialog.h"
#include "map_renderer.h"
#include "vector_map.h"

#include "lvgl.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <memory>
#include <vector>

namespace ais {

// The AIS viewer screen — a port of AdsbScreen. A header row (app name + count +
// conn dot) and a body that switches between List / PPI radar / Mercator map /
// Detail / Settings, all driven by a ~300 ms snapshot of the EntityStore. The
// rendering machinery (range rings, projection, trails, side lists, mini-radar)
// is shared with ADS-B; only the per-contact data are vessel fields.
class AisScreen : public screen::BaseScreen {
public:
    AisScreen(AisViewModel& vm,
              app::AssetManager& assets,
              toolkit::EntityStore& store,
              const toolkit::Config& config,
              std::function<bool()> conn_state);
    ~AisScreen() override;

protected:
    void build_content(lv_obj_t* content) override;
    bool show_title_bar() const override { return false; }
    bool overlay_nav_bar() const override { return false; }

private:
    static void tick_cb(lv_timer_t* timer);
    void tick();
    void open_location_dialog();
    static void list_draw_event_cb(lv_event_t* event);

    // A compact, sorted view row built from the store snapshot each tick.
    struct Row {
        std::string id;          // MMSI
        std::string name;        // AIS type 5 name (absent until a type 5 arrives)
        std::string callsign;    // AIS type 5 radio callsign
        std::string destination; // AIS type 5 voyage destination
        int ship_type{0};        // AIS type 5 ship-and-cargo code (0 = unknown)
        bool has_pos{false};
        toolkit::geo::LatLon pos{};
        bool has_sog{false};
        double sog{0.0};         // knots
        bool has_cog{false};
        double cog{0.0};         // degrees
        bool has_hdg{false};
        long hdg{0};             // degrees
        int  nav_status{-1};     // -1 absent, else 0-15
        bool has_seen{false};
        long seen{0};
        double range_nm{0.0};
        double bearing_deg{0.0};
    };

    std::vector<Row> build_all_rows();
    void apply_sort(std::vector<Row>& rows);
    int row_of(const std::vector<Row>& rows, const std::string& id) const;
    void update_header();
    void update_list(const std::vector<Row>& rows);
    void record_trails(const std::vector<Row>& rows);
    void render_scope(uint16_t* buf, int width, int height, lv_obj_t* canvas,
                      std::vector<lv_obj_t*>& ring_labels,
                      const std::vector<Row>& rows, int sel, bool show_others,
                      bool mercator = false);
    void update_ppi(const std::vector<Row>& rows);
    void update_detail(const std::vector<Row>& rows);
    void update_settings();
    void show_view(int screen);

    AisViewModel& vm_;
    toolkit::EntityStore& store_;
    toolkit::Config config_;
    std::function<bool()> conn_state_;

    // Header.
    lv_obj_t* header_       = nullptr;
    lv_obj_t* header_title_ = nullptr;
    lv_obj_t* header_count_ = nullptr;
    lv_obj_t* conn_dot_     = nullptr;

    // Body containers (one shown at a time).
    lv_obj_t* body_            = nullptr;
    lv_obj_t* list_view_       = nullptr;
    lv_obj_t* list_table_      = nullptr;
    lv_obj_t* ppi_canvas_      = nullptr;
    lv_obj_t* ppi_canvas_merc_ = nullptr;
    lv_obj_t* radar_left_      = nullptr;
    lv_obj_t* radar_right_     = nullptr;
    std::vector<lv_obj_t*> radar_left_rows_;
    std::vector<lv_obj_t*> radar_right_rows_;
    lv_obj_t* detail_box_     = nullptr;
    lv_obj_t* detail_label_   = nullptr;
    lv_obj_t* detail_values_  = nullptr;
    lv_obj_t* detail_names_b_ = nullptr;
    lv_obj_t* detail_values_b_ = nullptr;
    lv_obj_t* detail_msg_     = nullptr;
    lv_obj_t* detail_canvas_  = nullptr;
    lv_obj_t* settings_box_   = nullptr;
    lv_obj_t* settings_table_ = nullptr;
    int       settings_sel_row_ = -1;
    static void settings_draw_event_cb(lv_event_t* event);

    std::vector<lv_obj_t*> ppi_ring_labels_;
    std::vector<lv_obj_t*> detail_ring_labels_;
    std::vector<lv_color_t> list_row_colors_;
    int list_sel_row_ = -1;

    std::map<std::string, std::deque<toolkit::geo::LatLon>> trails_;

    std::vector<uint16_t> ppi_buf_;
    std::vector<uint16_t> ppi_buf_merc_;
    std::vector<uint16_t> detail_buf_;
    toolkit::map::VectorMap base_map_;
    std::unique_ptr<toolkit::LocationDialog> location_dialog_; // Settings > Location
    // Base-map pixels per canvas, replayed while the view is unchanged.
    toolkit::map::BaseMapCache scope_map_cache_;
    toolkit::map::BaseMapCache detail_map_cache_;

    const lv_font_t* font_small_ = nullptr;
    const lv_font_t* font_mono_  = nullptr;
    const lv_font_t* font_bold_  = nullptr;
    const lv_font_t* font_tiny_  = nullptr;

    int last_view_ = -1;
    lv_timer_t* timer_ = nullptr;
};

} // namespace ais
