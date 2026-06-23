/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "asset_manager.h"
#include "bindings.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace meshtastic {
namespace {

// UI refresh: snapshot the store a few times a second (the node list changes
// slowly — Meshtastic beacons are infrequent).
constexpr uint32_t kTickPeriodMs = 300;

// Generous ageing: nodes report rarely, so keep them around for an hour.
constexpr double kNodeTtlSeconds = 3600.0;

// Accent green from the chosen theme (radio-apps/09c), used for the self node.
constexpr const char* kSelfRecolor = "#63e2b7 ";

} // namespace

MeshtasticScreen::MeshtasticScreen(MeshtasticViewModel& vm,
                                   app::AssetManager& assets,
                                   toolkit::EntityStore& store)
    : BaseScreen(vm, vm, assets), vm_(vm), store_(store) {
    init();
}

MeshtasticScreen::~MeshtasticScreen() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void MeshtasticScreen::build_content(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    font_big_   = assets().load_font("inter-semibold.ttf", 22);
    font_small_ = assets().load_font("inter-regular.ttf", 12);
    const lv_font_t* fb = font_big_ ? font_big_ : &lv_font_montserrat_12;
    const lv_font_t* fs = font_small_ ? font_small_ : &lv_font_montserrat_12;

    // Placeholder big label, centred — shown on every page except NODES.
    view_label_ = lv_label_create(content);
    lv_label_set_text(view_label_, "CHATS");
    lv_obj_set_style_text_font(view_label_, fb, 0);
    reactive::bind_theme(view_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(view_label_, LV_ALIGN_CENTER, 0, -6);

    hint_label_ = lv_label_create(content);
    lv_label_set_text(hint_label_, "press 4 to switch view  -  ESC to quit");
    lv_obj_set_style_text_font(hint_label_, fs, 0);
    lv_obj_set_style_text_color(hint_label_, lv_color_hex(0x888888), 0);
    lv_obj_align(hint_label_, LV_ALIGN_CENTER, 0, 16);

    // NODES list: a recolor-enabled multi-line label (dim column header + rows;
    // the self node in accent green). Hidden until the NODES page.
    nodes_label_ = lv_label_create(content);
    lv_label_set_recolor(nodes_label_, true);
    lv_label_set_text(nodes_label_, "");
    lv_obj_set_width(nodes_label_, LV_PCT(100));
    lv_obj_set_style_text_font(nodes_label_, fs, 0);
    reactive::bind_theme(nodes_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(nodes_label_, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_obj_add_flag(nodes_label_, LV_OBJ_FLAG_HIDDEN);

    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);
    tick(); // initial paint
}

void MeshtasticScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<MeshtasticScreen*>(lv_timer_get_user_data(timer));
    if (self) {
        self->tick();
    }
}

void MeshtasticScreen::tick() {
    store_.sweep(kNodeTtlSeconds);
    const auto snap = store_.snapshot();
    const int page = vm_.page();

    // TitleBar subtitle: "<view> · N nodes" — visible on every page.
    char sub[48];
    std::snprintf(sub, sizeof(sub), "%s - %zu node%s", vm_.page_name(page),
                  snap.size(), snap.size() == 1 ? "" : "s");
    vm_.set_subtitle(sub);

    const bool nodes_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Nodes));
    lv_obj_set_flag(view_label_, LV_OBJ_FLAG_HIDDEN, nodes_page);
    lv_obj_set_flag(hint_label_, LV_OBJ_FLAG_HIDDEN, nodes_page);
    lv_obj_set_flag(nodes_label_, LV_OBJ_FLAG_HIDDEN, !nodes_page);

    if (!nodes_page) {
        lv_label_set_text(view_label_, vm_.page_name(page));
        return;
    }

    // Build the NODES list (dim header + one row per node; self in accent green).
    std::string text = "#888888 SHORT      SNR   HOP  AGE#\n";
    const long now = static_cast<long>(std::time(nullptr));
    int shown = 0;
    for (const auto& e : snap) {
        if (shown++ >= 8) break;
        const auto fld = [&](const char* k) -> std::string {
            const auto it = e.fields.find(k);
            return it != e.fields.end() ? it->second : std::string{};
        };
        std::string sh = fld("short");
        if (sh.empty()) sh = e.id;
        const std::string snr = fld("snr");
        const std::string hops = fld("hops");
        const std::string lh = fld("last_heard");

        std::string age = "-";
        if (!lh.empty()) {
            long a = now - std::atol(lh.c_str());
            if (a < 0) a = 0;
            char ab[16];
            if (a < 60) std::snprintf(ab, sizeof(ab), "%lds", a);
            else if (a < 3600) std::snprintf(ab, sizeof(ab), "%ldm", a / 60);
            else std::snprintf(ab, sizeof(ab), "%ldh", a / 3600);
            age = ab;
        }

        char line[128];
        std::snprintf(line, sizeof(line), "%-10.10s %5s  %3s  %s", sh.c_str(),
                      snr.empty() ? "-" : snr.c_str(),
                      hops.empty() ? "-" : hops.c_str(), age.c_str());

        if (!fld("self").empty()) {
            text += kSelfRecolor;
            text += line;
            text += "#\n";
        } else {
            text += line;
            text += "\n";
        }
    }
    if (shown == 0) {
        text += "(no nodes \xE2\x80\x94 is meshtasticd running on :4403?)";
    }
    lv_label_set_text(nodes_label_, text.c_str());
}

} // namespace meshtastic
