/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "battery.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace platform {

namespace {

std::string read_line(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

} // namespace

BatteryState read_battery() {
    BatteryState st;
    std::error_code ec;
    for (const auto& dev : std::filesystem::directory_iterator("/sys/class/power_supply", ec)) {
        if (read_line(dev.path() / "type") != "Battery") continue;
        const std::string cap = read_line(dev.path() / "capacity");
        if (cap.empty()) continue;
        st.present = true;
        st.percent = std::stoi(cap);
        if (st.percent < 0) st.percent = 0;
        if (st.percent > 100) st.percent = 100;
        const std::string status = read_line(dev.path() / "status");
        const std::string current = read_line(dev.path() / "current_now");
        // A few mA of noise either way on external power; charging draws hundreds.
        const bool current_in = !current.empty() && std::stol(current) > 50000;
        st.charging = status == "Charging" || status == "Full" || current_in;
        break;
    }
    return st;
}

} // namespace platform
