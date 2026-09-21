/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace platform {

struct BatteryState {
    bool present = false; // a battery power supply exists (false on the desktop)
    int percent = 0;      // 0-100
    bool charging = false;
};

// Reads the first POWER_SUPPLY_TYPE=Battery under /sys/class/power_supply (the
// CardputerZero's BQ27220 gauge). "Charging" is the gauge status Charging/Full
// or a positive current: on external power at 100% the gauge reports
// "Discharging" with a few mA either way, so the status alone is not enough.
BatteryState read_battery();

} // namespace platform
