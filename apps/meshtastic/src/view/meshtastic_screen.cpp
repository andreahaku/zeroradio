/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "meshtastic_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>

namespace meshtastic {
namespace {

constexpr uint32_t kTickPeriodMs = 300;
constexpr double kNodeTtlSeconds = 3600.0; // nodes beacon rarely; keep an hour

// NODES table columns (fit the 320px content width).
constexpr int32_t kColW[4] = {116, 64, 54, 62};
const char* const kColTitle[4] = {"SHORT", "SNR", "HOP", "AGE"};
constexpr int32_t kHeaderH = 15;

std::string field_of(const toolkit::Entity& e, const char* key) {
    const auto it = e.fields.find(key);
    return it != e.fields.end() ? it->second : std::string{};
}

} // namespace

MeshtasticScreen::MeshtasticScreen(MeshtasticViewModel& vm,
                                   app::AssetManager& assets,
                                   toolkit::EntityStore& store,
                                   MessageLog& messages,
                                   std::function<uint32_t(const std::string&)> on_send)
    : BaseScreen(vm, vm, assets),
      vm_(vm),
      store_(store),
      messages_(messages),
      on_send_(std::move(on_send)) {
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

    // CHATS feed: a recolour multi-line label (sender short name coloured, body in
    // theme text). Hidden until the CHATS page. Self in accent green, peers info-blue.
    chats_label_ = lv_label_create(content);
    lv_label_set_recolor(chats_label_, true);
    lv_label_set_long_mode(chats_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(chats_label_, "");
    lv_obj_set_width(chats_label_, LV_PCT(100));
    lv_obj_set_style_text_font(chats_label_, fs, 0);
    reactive::bind_theme(chats_label_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(chats_label_, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_obj_add_flag(chats_label_, LV_OBJ_FLAG_HIDDEN);

    // CHATS compose input row, pinned at the bottom; shown only while composing.
    compose_row_ = lv_label_create(content);
    lv_label_set_long_mode(compose_row_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(compose_row_, "> _");
    lv_obj_set_width(compose_row_, LV_PCT(100));
    lv_obj_set_style_text_font(compose_row_, fs, 0);
    lv_obj_set_style_bg_color(compose_row_, view::palette(vm_.is_dark_mode()).surface, 0);
    lv_obj_set_style_bg_opa(compose_row_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(compose_row_, view::palette(vm_.is_dark_mode()).primary, 0);
    lv_obj_set_style_border_width(compose_row_, 1, 0);
    lv_obj_set_style_pad_all(compose_row_, 3, 0);
    reactive::bind_theme(compose_row_, vm_.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_align(compose_row_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(compose_row_, LV_OBJ_FLAG_HIDDEN);

    // NODES view: a fixed column header strip + a themed table. Hidden by default.
    nodes_view_ = lv_obj_create(content);
    lv_obj_remove_style_all(nodes_view_);
    lv_obj_set_size(nodes_view_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(nodes_view_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(nodes_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(nodes_view_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(nodes_view_, 0, 0);
    lv_obj_set_style_pad_row(nodes_view_, 0, 0);
    lv_obj_add_flag(nodes_view_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* header = lv_obj_create(nodes_view_);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kHeaderH);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    int32_t hx = 0;
    for (int c = 0; c < 4; ++c) {
        lv_obj_t* h = lv_label_create(header);
        lv_label_set_text(h, kColTitle[c]);
        lv_obj_set_style_text_font(h, fs, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(0x888888), 0);
        lv_obj_align(h, LV_ALIGN_LEFT_MID, hx + 4, 0);
        hx += kColW[c];
    }

    nodes_table_ = lv_table_create(nodes_view_);
    lv_obj_set_width(nodes_table_, LV_PCT(100));
    lv_obj_set_flex_grow(nodes_table_, 1);
    lv_table_set_column_count(nodes_table_, 4);
    for (int c = 0; c < 4; ++c) lv_table_set_column_width(nodes_table_, c, kColW[c]);
    lv_obj_set_style_pad_all(nodes_table_, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_width(nodes_table_, 0, 0);
    lv_obj_set_style_text_font(nodes_table_, fs, LV_PART_ITEMS);
    lv_obj_remove_flag(nodes_table_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(nodes_table_, nodes_draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, this);
    lv_obj_add_flag(nodes_table_, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    // The "write" key bumps compose_req; observe it to enter compose mode.
    last_compose_req_ = lv_subject_get_int(vm_.compose_req_subject());
    lv_subject_add_observer(vm_.compose_req_subject(), compose_req_cb, this);

    timer_ = lv_timer_create(tick_cb, kTickPeriodMs, this);
    tick(); // initial paint
}

void MeshtasticScreen::compose_req_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<MeshtasticScreen*>(lv_observer_get_user_data(observer));
    if (!self) return;
    const int v = lv_subject_get_int(subject);
    if (v == self->last_compose_req_) return; // ignore the initial notification
    self->last_compose_req_ = v;
    // Only meaningful on the CHATS page; the "write" key only maps there anyway.
    if (self->vm_.page() == static_cast<int>(MeshtasticViewModel::Page::Chats)) {
        self->enter_compose();
    }
}

void MeshtasticScreen::compose_key_cb(uint32_t key, void* ctx) {
    if (auto* self = static_cast<MeshtasticScreen*>(ctx)) self->on_compose_key(key);
}

void MeshtasticScreen::enter_compose() {
    if (compose_active_) return;
    compose_active_ = true;
    compose_buf_.clear();
    update_compose_row();
    lv_obj_remove_flag(compose_row_, LV_OBJ_FLAG_HIDDEN);
    platform::set_key_capture(compose_key_cb, this);
}

void MeshtasticScreen::exit_compose() {
    platform::set_key_capture(nullptr, nullptr);
    compose_active_ = false;
    compose_buf_.clear();
    if (compose_row_) lv_obj_add_flag(compose_row_, LV_OBJ_FLAG_HIDDEN);
}

void MeshtasticScreen::on_compose_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        if (!compose_buf_.empty() && on_send_) on_send_(compose_buf_);
        exit_compose(); // modal per-message: send and return to BROWSE
    } else if (key == LV_KEY_ESC) {
        exit_compose(); // cancel
    } else if (key == LV_KEY_BACKSPACE) {
        if (!compose_buf_.empty()) {
            compose_buf_.pop_back();
            update_compose_row();
        }
    } else if (key >= 0x20 && key < 0x7f) {
        if (compose_buf_.size() < 200) {
            compose_buf_.push_back(static_cast<char>(key));
            update_compose_row();
        }
    }
}

void MeshtasticScreen::update_compose_row() {
    if (!compose_row_) return;
    std::string s = "> " + compose_buf_ + "_";
    lv_label_set_text(compose_row_, s.c_str());
}

void MeshtasticScreen::nodes_draw_event_cb(lv_event_t* event) {
    auto* self = static_cast<MeshtasticScreen*>(lv_event_get_user_data(event));
    auto* task = lv_event_get_draw_task(event);
    if (!self || !task) return;
    auto* base = static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
    if (!base) return;
    const uint32_t row = base->id1;
    const bool is_sel = (static_cast<int>(row) == self->nodes_sel_row_);
    const lv_draw_task_type_t type = lv_draw_task_get_type(task);

    if (type == LV_DRAW_TASK_TYPE_FILL && is_sel && base->part == LV_PART_ITEMS) {
        auto* fd = static_cast<lv_draw_fill_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        fd->color = view::palette(self->vm_.is_dark_mode()).primary;
        fd->opa = LV_OPA_COVER;
        return;
    }
    if (type == LV_DRAW_TASK_TYPE_LABEL) {
        auto* ld = static_cast<lv_draw_label_dsc_t*>(lv_draw_task_get_draw_dsc(task));
        if (is_sel) {
            ld->color = lv_color_black(); // contrast against the accent band
        } else if (row < self->nodes_row_colors_.size()) {
            ld->color = self->nodes_row_colors_[row];
        }
    }
}

void MeshtasticScreen::update_nodes(const std::vector<toolkit::Entity>& snap) {
    if (!nodes_table_) return;

    const auto pal = view::palette(vm_.is_dark_mode());
    const auto n = snap.size();
    lv_table_set_row_count(nodes_table_, static_cast<uint32_t>(n));
    nodes_row_colors_.assign(n, pal.text);

    const auto set_cell = [&](uint32_t r, uint16_t c, const char* v) {
        const char* cur = lv_table_get_cell_value(nodes_table_, r, c);
        if (!cur || std::strcmp(cur, v) != 0) {
            lv_table_set_cell_value(nodes_table_, r, c, v);
        }
    };

    const long now = static_cast<long>(std::time(nullptr));
    for (size_t i = 0; i < n; ++i) {
        const toolkit::Entity& e = snap[i];
        const bool self = !field_of(e, "self").empty();
        if (self) nodes_row_colors_[i] = pal.primary;

        std::string sh = field_of(e, "short");
        if (sh.empty()) sh = e.id;
        const std::string mark = self ? "\xE2\x97\x8f" : ""; // ● self marker
        const std::string call = mark + sh;

        const std::string snr = field_of(e, "snr");
        const std::string hops = field_of(e, "hops");
        const std::string lh = field_of(e, "last_heard");

        char age[16] = "-";
        if (!lh.empty()) {
            long a = now - std::atol(lh.c_str());
            if (a < 0) a = 0;
            if (a < 60) std::snprintf(age, sizeof(age), "%lds", a);
            else if (a < 3600) std::snprintf(age, sizeof(age), "%ldm", a / 60);
            else std::snprintf(age, sizeof(age), "%ldh", a / 3600);
        }

        set_cell(static_cast<uint32_t>(i), 0, call.c_str());
        set_cell(static_cast<uint32_t>(i), 1, snr.empty() ? "-" : snr.c_str());
        set_cell(static_cast<uint32_t>(i), 2, hops.empty() ? "-" : hops.c_str());
        set_cell(static_cast<uint32_t>(i), 3, age);
    }

    vm_.set_nodes_count(static_cast<int>(n));
    int cur = vm_.nodes_cursor();
    if (cur >= static_cast<int>(n)) cur = n > 0 ? static_cast<int>(n) - 1 : 0;
    nodes_sel_row_ = (n > 0) ? cur : -1;
    if (n > 0) lv_table_set_selected_cell(nodes_table_, static_cast<uint16_t>(cur), 0);
}

void MeshtasticScreen::update_chats(const std::vector<toolkit::Entity>& snap) {
    if (!chats_label_) return;

    // Map node number -> short name (id is "!aabbccdd") to label senders.
    std::unordered_map<uint32_t, std::string> names;
    for (const auto& e : snap) {
        if (e.id.size() > 1 && e.id[0] == '!') {
            const uint32_t num =
                static_cast<uint32_t>(std::strtoul(e.id.c_str() + 1, nullptr, 16));
            const std::string sh = field_of(e, "short");
            names[num] = sh.empty() ? e.id : sh;
        }
    }

    const auto msgs = messages_.snapshot();
    std::string text;
    const size_t start = msgs.size() > 9 ? msgs.size() - 9 : 0;
    for (size_t i = start; i < msgs.size(); ++i) {
        const MeshMessage& m = msgs[i];
        std::string sh;
        const auto it = names.find(m.from);
        if (it != names.end()) {
            sh = it->second;
        } else {
            char b[16];
            std::snprintf(b, sizeof(b), "!%08x", m.from);
            sh = b;
        }
        // Sender name recoloured (self accent-green, peers info-blue); body default.
        char hdr[48];
        std::snprintf(hdr, sizeof(hdr), "#%06x %s:# ",
                      m.is_self ? 0x63e2b7u : 0x70c0e8u, sh.c_str());
        text += hdr;
        text += m.text;
        // Delivery dot for our own messages: green ack / amber pending / red fail.
        if (m.is_self && m.ack != AckState::None) {
            const uint32_t c = m.ack == AckState::Delivered ? 0x63e2b7u
                               : m.ack == AckState::Failed  ? 0xe88080u
                                                            : 0xf0a020u;
            char dot[24];
            std::snprintf(dot, sizeof(dot), " #%06x \xE2\x97\x8f#", c);
            text += dot;
        }
        text += "\n";
    }
    if (msgs.empty()) {
        text = "#888888 (no messages yet)#";
    }
    lv_label_set_text(chats_label_, text.c_str());
}

void MeshtasticScreen::tick_cb(lv_timer_t* timer) {
    auto* self = static_cast<MeshtasticScreen*>(lv_timer_get_user_data(timer));
    if (self) self->tick();
}

void MeshtasticScreen::tick() {
    store_.sweep(kNodeTtlSeconds);
    const auto snap = store_.snapshot();
    const int page = vm_.page();

    char sub[48];
    std::snprintf(sub, sizeof(sub), "%s - %zu node%s", vm_.page_name(page),
                  snap.size(), snap.size() == 1 ? "" : "s");
    vm_.set_subtitle(sub);

    const bool nodes_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Nodes));
    const bool chats_page =
        (page == static_cast<int>(MeshtasticViewModel::Page::Chats));
    const bool placeholder = !nodes_page && !chats_page;

    lv_obj_set_flag(nodes_view_, LV_OBJ_FLAG_HIDDEN, !nodes_page);
    lv_obj_set_flag(chats_label_, LV_OBJ_FLAG_HIDDEN, !chats_page);
    lv_obj_set_flag(view_label_, LV_OBJ_FLAG_HIDDEN, !placeholder);
    lv_obj_set_flag(hint_label_, LV_OBJ_FLAG_HIDDEN, !placeholder);

    if (nodes_page) {
        update_nodes(snap);
    } else if (chats_page) {
        update_chats(snap);
    } else {
        lv_label_set_text(view_label_, vm_.page_name(page));
    }
}

} // namespace meshtastic
