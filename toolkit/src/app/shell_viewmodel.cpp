/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "shell_viewmodel.h"

namespace toolkit {

ShellViewModel::ShellViewModel() = default;

bool ShellViewModel::is_dark_mode() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(dark_mode_subject_.native())) != 0;
}

void ShellViewModel::set_dark_mode(bool enabled) {
    dark_mode_subject_.set(enabled);
}

void ShellViewModel::toggle_dark_mode() {
    dark_mode_subject_.toggle();
}

void ShellViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void ShellViewModel::request_handoff(std::string bin, std::string app_dir,
                                     std::vector<std::pair<std::string, std::string>> env) {
    handoff_ = {std::move(bin), std::move(app_dir), std::move(env)};
    request_quit();
}

void ShellViewModel::cycle_toolbar() {
    const int count = nav_page_count();
    const int next = count > 0 ? (toolbar_page_subject_.value() + 1) % count : 0;
    toolbar_page_subject_.set(next);
    bump_nav_refresh();
}

void ShellViewModel::cycle_toolbar_back() {
    const int count = nav_page_count();
    const int prev = count > 0 ? (toolbar_page_subject_.value() - 1 + count) % count : 0;
    toolbar_page_subject_.set(prev);
    bump_nav_refresh();
}

void ShellViewModel::bump_nav_refresh() {
    nav_refresh_subject_.set(nav_refresh_subject_.value() + 1);
}

void ShellViewModel::set_nav_provider(NavProvider* provider) {
    nav_provider_ = provider;
}

int ShellViewModel::nav_page_count() const {
    return nav_provider_ ? nav_provider_->nav_page_count() : 1;
}

bool ShellViewModel::nav_has_hold(int slot) const {
    if (slot == 0) return true;
    return nav_provider_ && nav_provider_->nav_has_hold(toolbar_page_subject_.value(), slot);
}

bool ShellViewModel::nav_repeats(int slot) const {
    return slot != 0 && nav_provider_ &&
           nav_provider_->nav_repeats(toolbar_page_subject_.value(), slot);
}

std::string ShellViewModel::nav_hold(int slot) {
    if (slot == 0) {
        cycle_toolbar_back();
        return {};
    }
    if (!nav_provider_) return {};
    return nav_provider_->nav_hold(toolbar_page_subject_.value(), slot);
}

i18n::Text ShellViewModel::nav_hint(int slot) const {
    if (slot == 0) {
        return {"Tap: the next page of keys. Hold: the page before.",
                "Tocca: pagina di tasti successiva. Tieni premuto: quella precedente.",
                "Appui : page de touches suivante. Maintien : la page précédente.",
                "Tippen: nächste Tastenseite. Halten: die vorherige.",
                "Pulsa: siguiente página de teclas. Mantén: la anterior.",
                "短押し：次のキーページ。長押し：前のページ。", "轻按：下一页按键。长按：上一页。"};
    }
    if (!nav_provider_ || slot < 0 || slot > 4) return {};
    return nav_provider_->nav_hint(toolbar_page_subject_.value(), slot);
}

lv_subject_t* ShellViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* ShellViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* ShellViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

lv_subject_t* ShellViewModel::toolbar_page_subject() {
    return toolbar_page_subject_.native();
}

lv_subject_t* ShellViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* ShellViewModel::subtitle_subject() {
    return subtitle_subject_.native();
}

lv_subject_t* ShellViewModel::nav_refresh_subject() {
    return nav_refresh_subject_.native();
}

void ShellViewModel::set_title(const char* text) {
    title_subject_.set(text ? text : "");
}

void ShellViewModel::set_subtitle(const char* text) {
    subtitle_subject_.set(text ? text : "");
}

} // namespace toolkit
