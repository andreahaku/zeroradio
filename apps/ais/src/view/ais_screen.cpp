/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "ais_screen.h"

#include "asset_manager.h"
#include "ui_const.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace ais {
namespace {

std::string field_or(const toolkit::Entity& e, const char* key, const char* fallback) {
    auto it = e.fields.find(key);
    return it != e.fields.end() ? it->second : fallback;
}

} // namespace

AisScreen::AisScreen(AisViewModel& vm,
                     app::AssetManager& assets,
                     toolkit::EntityStore& store,
                     std::function<bool()> conn_state)
    : screen::BaseScreen(vm, vm, assets), vm_(vm), store_(store),
      conn_state_(std::move(conn_state)) {}

AisScreen::~AisScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
    }
}

void AisScreen::build_content(lv_obj_t* content) {
    auto* font = assets().load_font("inter-regular.ttf", 12);

    table_ = lv_table_create(content);
    lv_obj_set_size(table_, lv_pct(100), lv_pct(100));
    lv_obj_align(table_, LV_ALIGN_TOP_LEFT, 0, 0);
    if (font) {
        lv_obj_set_style_text_font(table_, font, LV_PART_ITEMS);
    }
    lv_obj_set_style_pad_all(table_, 2, LV_PART_ITEMS);

    lv_table_set_column_count(table_, 4);
    lv_table_set_column_width(table_, 0, 88);  // MMSI
    lv_table_set_column_width(table_, 1, 132); // lat,lon
    lv_table_set_column_width(table_, 2, 48);  // SOG
    lv_table_set_column_width(table_, 3, 48);  // COG
    lv_table_set_cell_value(table_, 0, 0, "MMSI");
    lv_table_set_cell_value(table_, 0, 1, "Lat,Lon");
    lv_table_set_cell_value(table_, 0, 2, "SOG");
    lv_table_set_cell_value(table_, 0, 3, "COG");

    timer_ = lv_timer_create(tick_cb, 500, this);
    tick();
}

void AisScreen::tick_cb(lv_timer_t* timer) {
    static_cast<AisScreen*>(lv_timer_get_user_data(timer))->tick();
}

void AisScreen::tick() {
    auto entities = store_.snapshot();
    std::sort(entities.begin(), entities.end(),
              [](const toolkit::Entity& a, const toolkit::Entity& b) { return a.id < b.id; });

    lv_table_set_row_count(table_, static_cast<uint32_t>(entities.size()) + 1);
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        const auto row = static_cast<uint32_t>(i + 1);

        lv_table_set_cell_value(table_, row, 0, e.id.c_str());

        char pos[40];
        if (e.has_pos) {
            std::snprintf(pos, sizeof(pos), "%.4f,%.4f", e.pos.lat, e.pos.lon);
        } else {
            std::snprintf(pos, sizeof(pos), "--");
        }
        lv_table_set_cell_value(table_, row, 1, pos);
        lv_table_set_cell_value(table_, row, 2, field_or(e, "sog", "--").c_str());
        lv_table_set_cell_value(table_, row, 3, field_or(e, "cog", "--").c_str());
    }

    char sub[24];
    std::snprintf(sub, sizeof(sub), "%zu %s", entities.size(),
                  conn_state_ && conn_state_() ? "ships" : "ships (no feed)");
    vm_.set_subtitle(sub);
}

} // namespace ais
