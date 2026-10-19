/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "squelch.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

} // namespace

int main() {
    sdr::Squelch gate;
    check(gate.open(), "disabled gate starts open");
    gate.update(0, 0);
    check(gate.open(), "zero threshold stays open without signal");

    gate.set_threshold(50);
    check(!gate.open(), "enabling threshold starts closed");
    gate.update(49, 100);
    check(!gate.open(), "level below threshold does not open");
    gate.update(50, 200);
    check(gate.open(), "level at threshold opens");

    gate.update(42, 300); // lower edge is 42 with 8 points of hysteresis
    gate.update(45, 5000);
    check(gate.open(), "hysteresis band stays open without a timer");
    gate.update(41, 5100);
    gate.update(41, 7599);
    check(gate.open(), "short pause does not close");
    gate.update(42, 7600); // recovery into the hysteresis band resets hold
    gate.update(41, 7700);
    gate.update(41, 10199);
    check(gate.open(), "recovery restarts the hold period");
    gate.update(41, 10200);
    check(!gate.open(), "continuous low signal closes after hold");
    gate.update(49, 10300);
    check(!gate.open(), "closed gate waits for opening threshold");
    gate.update(55, 10400);
    check(gate.open(), "signal reopens after a closed period");

    gate.update(0, 11000);
    gate.update(50, 12000);
    gate.update(0, 13000);
    gate.update(0, 15499);
    check(gate.open(), "strong signal during hold cancels closure");
    gate.update(0, 15500);
    check(!gate.open(), "second hold expires from its own start");

    gate.set_threshold(0);
    check(gate.open(), "disabling threshold immediately opens");
    gate.set_threshold(5);
    gate.update(5, 16000);
    gate.update(0, 17000);
    gate.update(0, 19500);
    check(!gate.open(), "low threshold can still close at silence");

    sdr::Squelch immediate(60, 8, 0);
    immediate.update(60, 0);
    immediate.update(0, 1);
    check(!immediate.open(), "zero hold closes immediately");

    if (failures == 0) std::printf("squelch_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
