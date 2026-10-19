/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// sdr::SdrModel: the frequency correction (sign, size, clamping) and loading
// an older state file.

#include "sdr_model.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

std::filesystem::path state_file(const char* dir) {
    return std::filesystem::path(dir) / "zeroradio" / "sdr" / "state";
}

void write_state(const char* dir, const std::string& line) {
    std::filesystem::create_directories(state_file(dir).parent_path());
    std::ofstream(state_file(dir), std::ios::trunc) << line << '\n';
}

} // namespace

int main() {
    char tmpl[] = "/tmp/sdr_model_test_XXXXXX";
    const char* dir = mkdtemp(tmpl);
    check(dir != nullptr, "temporary directory");
    if (!dir) return 1;
    setenv("XDG_CONFIG_HOME", dir, 1);
    unsetenv("SDR_FREQ");
    unsetenv("SDR_MODE");

    // A v4 file (before the correction existed) still loads, with no correction.
    write_state(dir, "4 127400000 2 50 0 2 0 0 1 1 1 0 1 297");
    {
        sdr::SdrModel m;
        check(m.vfo_hz() == 127400000, "v4: frequency loads");
        check(m.mode() == sdr::RadioMode::AM, "v4: mode loads");
        check(m.ppm_tenths() == 0, "v4: no correction");
        check(m.corrected_hz(127400000) == 127400000, "no correction leaves the frequency alone");

        // A fast crystal (+1.0 ppm) tunes high, so the tuner is asked for less:
        // 100 MHz / (1 + 1e-6) = 99,999,900 Hz. A multiply would give 100,000,100.
        m.set_ppm_tenths(10);
        check(m.corrected_hz(100000000) == 99999900, "+1.0 ppm divides, 100 Hz down at 100 MHz");
        m.set_ppm_tenths(-24);
        check(m.corrected_hz(127400000) == 127400306, "-2.4 ppm, 306 Hz up at 127.4 MHz");
        check(m.corrected_hz(0) == 0, "zero frequency is left alone");

        m.set_ppm_tenths(5000);
        check(m.ppm_tenths() == sdr::SdrModel::kMaxPpmTenths, "clamped high");
        m.set_ppm_tenths(-5000);
        check(m.ppm_tenths() == -sdr::SdrModel::kMaxPpmTenths, "clamped low");
        m.set_ppm_tenths(-24);
        m.ppm_up();
        check(m.ppm_tenths() == -23, "step up by 0.1 ppm");
    }

    // The correction is saved and comes back.
    {
        sdr::SdrModel m;
        check(m.ppm_tenths() == -23, "correction saved and loaded");
        check(m.vfo_hz() == 127400000, "frequency kept across the upgrade");
    }

    // A correction outside the range in the file is ignored.
    write_state(dir, "5 127400000 2 50 0 2 0 0 1 1 1 0 1 297 99999");
    {
        sdr::SdrModel m;
        check(m.ppm_tenths() == 0, "out-of-range correction ignored");
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (failures == 0) std::printf("sdr_model_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
