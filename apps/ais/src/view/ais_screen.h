/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ais_viewmodel.h"
#include "base_screen.h"
#include "entity_store.h"

#include "lvgl.h"

#include <functional>

namespace ais {

// First-cut AIS viewer: a TitleBar + a scrolling table of vessels (MMSI,
// position, SOG, COG) snapshotted from the EntityStore on a ~500 ms timer. The
// radar/Mercator map view (reusing apps/adsb's render_scope) is a follow-up.
class AisScreen : public screen::BaseScreen {
public:
    AisScreen(AisViewModel& vm,
              app::AssetManager& assets,
              toolkit::EntityStore& store,
              std::function<bool()> conn_state);
    ~AisScreen() override;

protected:
    void build_content(lv_obj_t* content) override;
    bool show_title_bar() const override { return true; }

private:
    static void tick_cb(lv_timer_t* timer);
    void tick();

    AisViewModel& vm_;
    toolkit::EntityStore& store_;
    std::function<bool()> conn_state_;
    lv_obj_t* table_{nullptr};
    lv_timer_t* timer_{nullptr};
};

} // namespace ais
