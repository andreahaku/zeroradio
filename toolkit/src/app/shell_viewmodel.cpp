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

void ShellViewModel::cycle_toolbar() {
    const int count = nav_page_count();
    const int next = count > 0 ? (toolbar_page_subject_.value() + 1) % count : 0;
    toolbar_page_subject_.set(next);
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
