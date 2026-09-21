/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

// Base-map renderer check + timing. For a set of views it renders with and
// without culling and requires identical pixels; it also prints the time per
// frame, so the same binary benchmarks a dataset on the device:
//   map_render_test [path/to/map.rmap]   (default: assets/mapdata/world.rmap)

#include "map_renderer.h"
#include "vector_map.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef MAP_TEST_DEFAULT
#define MAP_TEST_DEFAULT "assets/mapdata/world.rmap"
#endif

using namespace toolkit;

namespace {

struct Case {
    const char* name;
    geo::LatLon home;
    double range_nm;
    map::Projection proj;
};

double render_ms(const map::MapViewport& vp, const map::VectorMap& m,
                 const map::MapStyle& st, std::vector<uint16_t>& buf, int reps) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) {
        std::fill(buf.begin(), buf.end(), 0);
        map::draw_base(buf.data(), vp, m, st);
    }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
}

} // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : MAP_TEST_DEFAULT;
    const auto m = map::VectorMap::load(path);
    if (!m.valid()) {
        std::fprintf(stderr, "cannot load %s\n", path.c_str());
        return 1;
    }

    const Case cases[] = {
        {"Valletta merc 50nm", {35.90, 14.51}, 50.0, map::Projection::Mercator},
        {"Valletta radar 100nm", {35.90, 14.51}, 100.0, map::Projection::Azimuthal},
        {"Bologna merc 20nm", {44.49, 11.34}, 20.0, map::Projection::Mercator},
        {"Tokyo radar 200nm", {35.68, 139.69}, 200.0, map::Projection::Azimuthal},
        {"Fiji antimeridian merc", {-17.7, 179.9}, 100.0, map::Projection::Mercator},
        {"Svalbard merc", {78.2, 15.6}, 100.0, map::Projection::Mercator},
    };

    map::MapViewport vp;
    vp.width = 212;
    vp.height = 106;
    vp.cx = vp.width / 2;
    vp.cy = vp.height / 2;
    vp.radius_px = vp.height / 2;
    std::vector<uint16_t> a(static_cast<size_t>(vp.width) * vp.height);
    std::vector<uint16_t> b(a.size());

    map::MapStyle culled;
    map::MapStyle full;
    full.cull = false;

    int failures = 0;
    for (const auto& c : cases) {
        vp.home = c.home;
        vp.range_nm = c.range_nm;
        vp.projection = c.proj;
        const double t_full = render_ms(vp, m, full, b, 3);
        const double t_cull = render_ms(vp, m, culled, a, 3);
        const bool same = std::memcmp(a.data(), b.data(), a.size() * sizeof(uint16_t)) == 0;
        if (!same) ++failures;
        std::printf("%-24s full %8.2f ms  culled %8.2f ms  %s\n", c.name, t_full, t_cull,
                    same ? "identical" : "PIXELS DIFFER");
    }
    return failures == 0 ? 0 : 1;
}
