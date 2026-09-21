/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "hub_viewmodel.h"

namespace radio {

HubViewModel::HubViewModel(const std::vector<AppEntry>& apps) : apps_(apps) {
    set_title("ZeroRadio " APP_VERSION);
}

int HubViewModel::count() const {
    return static_cast<int>(apps_.size());
}

const AppEntry& HubViewModel::entry(int index) const {
    return apps_.at(static_cast<size_t>(index));
}

lv_subject_t* HubViewModel::selected_subject() {
    return selected_subject_.native();
}

int HubViewModel::selected() const {
    return selected_subject_.value();
}

void HubViewModel::move_up() {
    if (apps_.empty()) {
        return;
    }
    const int n = count();
    selected_subject_.set((selected() - 1 + n) % n);
}

void HubViewModel::move_down() {
    if (apps_.empty()) {
        return;
    }
    selected_subject_.set((selected() + 1) % count());
}

void HubViewModel::launch_selected() {
    if (apps_.empty()) {
        return;
    }
    launch_entry_ = &entry(selected());
    launch_target_ = launch_entry_->id;
    request_quit(); // ends the LVGL loop; the outer run loop reads launch_target()
}

const std::string& HubViewModel::launch_target() const {
    return launch_target_;
}

const AppEntry* HubViewModel::launch_entry() const {
    return launch_entry_;
}

} // namespace radio
