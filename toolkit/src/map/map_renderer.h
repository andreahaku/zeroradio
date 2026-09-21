/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "geo.h"
#include "vector_map.h"

#include <cstdint>
#include <vector>

namespace toolkit::map {

// Which projection the scope is drawn with. Azimuthal == the existing PPI radar
// (range/bearing from home); Mercator == the conformal "map" view.
enum class Projection {
    Azimuthal,
    Mercator,
};

// Where and how big the scope is on an RGB565 canvas, plus what it's centred on
// and how far it reaches. `range_nm` is the azimuthal outer-ring distance, or
// the Mercator vertical half-span, in nautical miles.
struct MapViewport {
    int width = 0;
    int height = 0;
    int cx = 0;
    int cy = 0;
    int radius_px = 0;
    geo::LatLon home{};
    double range_nm = 1.0;
    Projection projection = Projection::Mercator;
};

// Pack an 8-8-8 colour into RGB565.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Dark base-map palette (RGB565). Land is filled black over a barely-there
// blue-grey sea, with a faint coastline tracing the boundary — chosen to read
// as "land vs sea" with minimal contrast under the bright entity dots.
struct MapStyle {
    uint16_t sea_color = rgb565(0x12, 0x1a, 0x22);   // faint blue-grey sea
    uint16_t land_color = rgb565(0x00, 0x00, 0x00);  // black land
    uint16_t coast_color = rgb565(0x3c, 0x4b, 0x5a); // faint grey-blue coastline
    uint16_t border_color = rgb565(0x42, 0x42, 0x4a); // dim slate borders, dashed
    bool border_dashed = true;
    // Skip geometry outside the view before projecting it. Output is identical
    // either way (checked by map_render_test); off only for that comparison.
    bool cull = true;

    // Daylight variant for the Light theme: pale sea, near-white land and dark
    // coast/border lines, readable in direct sunlight.
    static MapStyle day() {
        MapStyle s;
        s.sea_color = rgb565(0xd6, 0xe4, 0xee);
        s.land_color = rgb565(0xfb, 0xfa, 0xf6);
        s.coast_color = rgb565(0x2b, 0x40, 0x55);
        s.border_color = rgb565(0x78, 0x7e, 0x88);
        return s;
    }
    bool operator==(const MapStyle& o) const {
        return sea_color == o.sea_color && land_color == o.land_color &&
               coast_color == o.coast_color && border_color == o.border_color &&
               border_dashed == o.border_dashed && cull == o.cull;
    }
};

// Draw the coastline + national-border layers of `map` onto an RGB565 buffer.
// Does NOT clear the buffer or draw rings — the caller owns the background and
// plots entity dots on top afterwards. Polylines are projected with the
// viewport's projection and clipped to the canvas (Mercator: rectangle;
// Azimuthal: the radius circle). A no-op if the map is invalid.
void draw_base(uint16_t* buf, const MapViewport& vp,
               const VectorMap& map, const MapStyle& style);

// draw_base() for a view that is redrawn every tick but rarely moves: the base
// map only changes with home, range, size or projection, so the pixels of the
// last render are replayed while the viewport is unchanged. One cache per
// canvas. The caller must prepare the same background before every call (the
// radar views clear to black), since a hit replays the whole buffer.
class BaseMapCache {
public:
    void draw(uint16_t* buf, const MapViewport& vp, const VectorMap& map,
              const MapStyle& style = MapStyle{});

private:
    std::vector<uint16_t> pixels_;
    MapViewport last_{};
    MapStyle last_style_{};
    const VectorMap* map_ = nullptr;
    bool valid_ = false;
};

} // namespace toolkit::map
