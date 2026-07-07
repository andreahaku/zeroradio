/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// Characterization test for the shared waterfall row primitive
// (view::push_waterfall_row) and the colormap it writes with
// (view::colormap_rgb565).
//
// Two-source discipline (same pattern as the AIS / Meshtastic / Survey parity
// tests): the expected colours come from (a) hand-derived RGB565 literals at
// the gradient's exact stops and (b) an independent reference implementation
// written from the documented spec ("piecewise-linear black -> blue -> cyan ->
// yellow -> red, u8 rounding, RGB565 packing") — NOT copied from the toolkit
// code under test. The scroll contract is asserted structurally.
//
// This file is frozen (reward-guard): do not edit it to make code pass.

#include "raster.h"

#include <cstdint>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            std::printf("FAIL %s:%d  ", __FILE__, __LINE__);                   \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// Independent reference (second source), written from the documented spec.
// ---------------------------------------------------------------------------

static uint8_t ref_to_u8(float c) {
    int x = static_cast<int>(c * 255.0f + 0.5f);
    if (x < 0) x = 0;
    if (x > 255) x = 255;
    return static_cast<uint8_t>(x);
}

static uint16_t ref_pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t ref_colormap(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    float r, g, b;
    if (v < 0.25f) {            // black -> blue
        const float t = v / 0.25f;
        r = 0.0f; g = 0.0f; b = t;
    } else if (v < 0.5f) {      // blue -> cyan
        const float t = (v - 0.25f) / 0.25f;
        r = 0.0f; g = t; b = 1.0f;
    } else if (v < 0.75f) {     // cyan -> yellow
        const float t = (v - 0.5f) / 0.25f;
        r = t; g = 1.0f; b = 1.0f - t;
    } else {                    // yellow -> red
        const float t = (v - 0.75f) / 0.25f;
        r = 1.0f; g = 1.0f - t; b = 0.0f;
    }
    return ref_pack_rgb565(ref_to_u8(r), ref_to_u8(g), ref_to_u8(b));
}

// ---------------------------------------------------------------------------
// 1. Colormap anchors — hand-derived literals at the gradient stops + clamp.
// ---------------------------------------------------------------------------

static void test_colormap_anchors() {
    struct Anchor { float v; uint16_t expected; const char* name; };
    static const Anchor kAnchors[] = {
        {0.00f, 0x0000, "black"},
        {0.25f, 0x001F, "pure blue"},
        {0.50f, 0x07FF, "cyan"},
        {0.75f, 0xFFE0, "yellow"},
        {1.00f, 0xF800, "pure red"},
        {-1.0f, 0x0000, "clamped low -> black"},
        {2.00f, 0xF800, "clamped high -> red"},
    };
    for (const auto& a : kAnchors) {
        const uint16_t got = view::colormap_rgb565(a.v);
        CHECK(got == a.expected, "colormap(%f) = 0x%04X, expected 0x%04X (%s)",
              static_cast<double>(a.v), got, a.expected, a.name);
    }
}

// ---------------------------------------------------------------------------
// 2. Colormap vs the independent reference across all branches.
// ---------------------------------------------------------------------------

static void test_colormap_reference() {
    // Dense sweep (freeze-review hardening): 241 samples from -0.1 to 1.1 in
    // 0.005 steps cover every branch, both clamps, the exact branch edges and
    // the near-boundary/rounding-stress values — a sparse lookup table cannot
    // green this.
    for (int i = -20; i <= 220; ++i) {
        const float v = static_cast<float>(i) * 0.005f;
        const uint16_t got = view::colormap_rgb565(v);
        const uint16_t exp = ref_colormap(v);
        CHECK(got == exp, "colormap(%f) = 0x%04X, reference 0x%04X",
              static_cast<double>(v), got, exp);
    }
    // Explicit epsilon probes right around the branch boundaries.
    static const float kEdges[] = {0.24999f, 0.25001f, 0.49999f, 0.50001f,
                                   0.74999f, 0.75001f};
    for (float v : kEdges) {
        const uint16_t got = view::colormap_rgb565(v);
        const uint16_t exp = ref_colormap(v);
        CHECK(got == exp, "colormap(%f) = 0x%04X, reference 0x%04X (edge probe)",
              static_cast<double>(v), got, exp);
    }
}

// ---------------------------------------------------------------------------
// 3. push_waterfall_row — scroll + top-row write + bounds contract.
// ---------------------------------------------------------------------------

static void test_push_scroll() {
    constexpr int kW = 11;
    constexpr int kH = 4;

    // One guard row beyond the buffer the function is told about: it must
    // never be written (the function's write extent is exactly w*h).
    std::vector<uint16_t> buf(static_cast<size_t>(kW) * (kH + 1));
    const uint16_t kGuard = 0xBEEF;
    for (int x = 0; x < kW; ++x) buf[static_cast<size_t>(kH) * kW + x] = kGuard;

    // Pre-fill the visible rows with a row-distinct sentinel pattern.
    auto sentinel = [](int row, int x) {
        return static_cast<uint16_t>(row * 1000 + x);
    };
    for (int row = 0; row < kH; ++row)
        for (int x = 0; x < kW; ++x) buf[static_cast<size_t>(row) * kW + x] = sentinel(row, x);

    // First push: one value per colormap branch + both clamps.
    static const float kMags1[kW] = {-0.5f, 0.0f, 0.1f, 0.25f, 0.4f, 0.5f,
                                     0.6f,  0.75f, 0.9f, 1.0f, 1.5f};
    view::push_waterfall_row(buf.data(), kW, kH, kMags1);

    // Rows 1..h-1 must now hold what rows 0..h-2 held (scrolled down by one).
    for (int row = 1; row < kH; ++row)
        for (int x = 0; x < kW; ++x)
            CHECK(buf[static_cast<size_t>(row) * kW + x] == sentinel(row - 1, x),
                  "after push 1: row %d col %d = %u, expected shifted sentinel %u",
                  row, x, buf[static_cast<size_t>(row) * kW + x], sentinel(row - 1, x));

    // The new top row is the colormapped mags.
    for (int x = 0; x < kW; ++x)
        CHECK(buf[x] == ref_colormap(kMags1[x]),
              "after push 1: top col %d = 0x%04X, expected 0x%04X",
              x, buf[x], ref_colormap(kMags1[x]));

    // Second push: uniform mid value. Top = cyan row, row 1 = previous top.
    float mags2[kW];
    for (int x = 0; x < kW; ++x) mags2[x] = 0.5f;
    view::push_waterfall_row(buf.data(), kW, kH, mags2);

    for (int x = 0; x < kW; ++x)
        CHECK(buf[x] == 0x07FF, "after push 2: top col %d = 0x%04X, expected cyan", x, buf[x]);
    for (int x = 0; x < kW; ++x)
        CHECK(buf[static_cast<size_t>(kW) + x] == ref_colormap(kMags1[x]),
              "after push 2: row 1 col %d should be push-1 colours", x);
    // Rows 2..h-1 keep scrolling the sentinels down.
    for (int row = 2; row < kH; ++row)
        for (int x = 0; x < kW; ++x)
            CHECK(buf[static_cast<size_t>(row) * kW + x] == sentinel(row - 2, x),
                  "after push 2: row %d col %d lost the scrolled sentinel", row, x);

    // The guard row past h was never touched.
    for (int x = 0; x < kW; ++x)
        CHECK(buf[static_cast<size_t>(kH) * kW + x] == kGuard,
              "guard row col %d overwritten (0x%04X) — wrote past w*h", x,
              buf[static_cast<size_t>(kH) * kW + x]);
}

// ---------------------------------------------------------------------------
// 4. Degenerate size: h == 1 (no scroll source) must still write the top row.
// ---------------------------------------------------------------------------

static void test_push_single_row() {
    constexpr int kW = 3;
    uint16_t buf[kW] = {1, 2, 3};
    static const float kMags[kW] = {0.0f, 0.5f, 1.0f};
    view::push_waterfall_row(buf, kW, 1, kMags);
    CHECK(buf[0] == 0x0000 && buf[1] == 0x07FF && buf[2] == 0xF800,
          "h=1: top row = {0x%04X, 0x%04X, 0x%04X}, expected black/cyan/red",
          buf[0], buf[1], buf[2]);
}

// ---------------------------------------------------------------------------
// 5. Minimal overlapping scroll: w == 1, h == 2 (freeze-review hardening).
// ---------------------------------------------------------------------------

static void test_push_minimal() {
    uint16_t buf[2] = {0x1234, 0x5678};
    static const float kMag[1] = {1.0f};
    view::push_waterfall_row(buf, 1, 2, kMag);
    CHECK(buf[1] == 0x1234, "w=1,h=2: old top must scroll to row 1 (got 0x%04X)", buf[1]);
    CHECK(buf[0] == 0xF800, "w=1,h=2: new top must be red (got 0x%04X)", buf[0]);
}

int main() {
    test_colormap_anchors();
    test_colormap_reference();
    test_push_scroll();
    test_push_single_row();
    test_push_minimal();

    if (failures == 0) {
        std::printf("waterfall_scroll_test: all checks passed\n");
        return 0;
    }
    std::printf("waterfall_scroll_test: %d check(s) FAILED\n", failures);
    return 1;
}
