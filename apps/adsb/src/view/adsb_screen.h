/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "adsb_viewmodel.h"
#include "app_config.h"
#include "base_screen.h"
#include "entity_store.h"

#include "lvgl.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace adsb {

// The ADS-B viewer screen: a header row (app name + "N trk" + conn dot) and a
// body that switches between List / PPI / Detail by the viewmodel's view_mode.
// A ~300 ms timer snapshots the EntityStore, sorts it, and refreshes the active
// view. The TitleBar is hidden; a solid NavBar drives the 5 keys.
class AdsbScreen : public screen::BaseScreen {
public:
    AdsbScreen(AdsbViewModel& vm,
               app::AssetManager& assets,
               toolkit::EntityStore& store,
               const toolkit::Config& config,
               std::function<bool()> conn_state);
    ~AdsbScreen() override;

protected:
    void build_content(lv_obj_t* content) override;
    bool show_title_bar() const override { return false; }
    bool overlay_nav_bar() const override { return false; }

private:
    static void tick_cb(lv_timer_t* timer);
    void tick();

    // A compact, sorted view row built from the store snapshot each tick.
    struct Row {
        std::string hex;
        std::string flight;
        bool has_pos{false};
        toolkit::geo::LatLon pos{};
        bool has_alt{false};
        long alt{0};
        bool on_ground{false};
        bool has_gs{false};
        long gs{0};
        bool has_track{false};
        long track{0};
        std::string squawk;
        std::string category;
        bool emergency{false};
        long seen{0};
        double range_nm{0.0};   // computed from home when has_pos
        double bearing_deg{0.0};
    };

    std::vector<Row> build_rows();   // snapshot + sort per the viewmodel
    // Index in `rows` of the viewmodel's selected hex (0 if absent/empty).
    int selected_row(const std::vector<Row>& rows) const;
    void update_header(int track_count);
    void update_list(const std::vector<Row>& rows);
    void update_ppi(const std::vector<Row>& rows);
    void update_detail(const std::vector<Row>& rows);
    void show_view(int view_mode);

    AdsbViewModel& vm_;
    toolkit::EntityStore& store_;
    toolkit::Config config_;
    std::function<bool()> conn_state_;

    // Header.
    lv_obj_t* header_         = nullptr;
    lv_obj_t* header_title_   = nullptr;
    lv_obj_t* header_count_   = nullptr;
    lv_obj_t* conn_dot_       = nullptr;

    // Body containers (one shown at a time).
    lv_obj_t* body_           = nullptr;
    lv_obj_t* list_table_     = nullptr;
    lv_obj_t* ppi_canvas_     = nullptr;
    lv_obj_t* detail_box_     = nullptr;
    lv_obj_t* detail_label_   = nullptr;

    // Pooled callsign labels overlaid on the PPI (positioned per aircraft each
    // tick; unused ones are hidden). Children of body_, so they sit on the canvas.
    std::vector<lv_obj_t*> ppi_labels_;
    // Range-ring scale labels (NM at each ring), one per concentric ring.
    std::vector<lv_obj_t*> ppi_ring_labels_;

    // PPI canvas backing store (RGB565).
    std::vector<uint16_t> ppi_buf_;

    const lv_font_t* font_small_ = nullptr;
    const lv_font_t* font_mono_  = nullptr;

    int last_view_ = -1;
    lv_timer_t* timer_ = nullptr;
};

} // namespace adsb
