/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "map_renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace toolkit::map {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Project a lat/lon to absolute canvas pixels. Never culls: out-of-view points
// come back as off-canvas pixels so segment clipping can trim them at the edge.
void project_point(const MapViewport& vp, geo::LatLon p, double& px, double& py) {
    int dx = 0, dy = 0;
    if (vp.projection == Projection::Mercator) {
        geo::project_mercator(vp.home, p, vp.range_nm, vp.radius_px, dx, dy);
    } else {
        // Azimuthal, but unculled (geo::project() drops points beyond range).
        const double range = geo::range_nm(vp.home, p);
        const double brg = geo::bearing_deg(vp.home, p) * kPi / 180.0;
        const double r = (range / vp.range_nm) * static_cast<double>(vp.radius_px);
        dx = static_cast<int>(std::lround(r * std::sin(brg)));
        dy = static_cast<int>(std::lround(-r * std::cos(brg)));
    }
    px = static_cast<double>(vp.cx + dx);
    py = static_cast<double>(vp.cy + dy);
}

// Bresenham line into the RGB565 buffer. `circle` clips each pixel to the radius
// circle (azimuthal radar); otherwise the canvas rectangle bounds clip it.
// `dash` skips alternate runs for dashed borders.
void plot_line(uint16_t* buf, const MapViewport& vp, int x0, int y0, int x1, int y1,
               uint16_t color, bool circle, bool dash) {
    const long r2 = static_cast<long>(vp.radius_px) * vp.radius_px;
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int step = 0;
    for (;;) {
        if (x0 >= 0 && x0 < vp.width && y0 >= 0 && y0 < vp.height &&
            (!dash || ((step >> 1) & 1) == 0)) {
            bool draw = true;
            if (circle) {
                const long ex = x0 - vp.cx, ey = y0 - vp.cy;
                draw = (ex * ex + ey * ey) <= r2;
            }
            if (draw) buf[y0 * vp.width + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        ++step;
    }
}

// The part of the map that can reach the canvas, as a quantized box. Geometry
// entirely outside it is skipped before projection: with a detailed worldwide
// dataset almost everything is out of view, and projecting it every frame is
// what costs time on the device. Conservative (20% margin); longitude culling
// is dropped near the poles or across the antimeridian.
struct QBox {
    QPoint lo{0, 0};
    QPoint hi{65535, 65535};
    bool intersects(const Polyline& p) const {
        return p.hi.x >= lo.x && p.lo.x <= hi.x && p.hi.y >= lo.y && p.lo.y <= hi.y;
    }
};

QBox view_box(const MapViewport& vp, const VectorMap& map) {
    QBox box;
    // Farthest distance from home that can show on the canvas, in NM (the canvas
    // corner for Mercator; the ring radius already bounds the azimuthal view).
    const double half_px = 0.5 * std::max(vp.width, vp.height);
    const double ext_nm = 1.2 * vp.range_nm * std::max(1.0, half_px / vp.radius_px);
    const double dlat = ext_nm / 60.0;

    auto q = [](double v, double lo, double hi) {
        const double t = (v - lo) / (hi - lo);
        return static_cast<uint16_t>(std::clamp(t, 0.0, 1.0) * 65535.0);
    };
    box.lo.y = q(vp.home.lat - dlat, map.min_lat(), map.max_lat());
    box.hi.y = q(vp.home.lat + dlat, map.min_lat(), map.max_lat());

    const double edge_lat = std::fabs(vp.home.lat) + dlat;
    if (edge_lat < 85.0) {
        const double dlon = dlat / std::cos(edge_lat * kPi / 180.0);
        const double w = vp.home.lon - dlon, e = vp.home.lon + dlon;
        if (w > -180.0 && e < 180.0) {
            box.lo.x = q(w, map.min_lon(), map.max_lon());
            box.hi.x = q(e, map.min_lon(), map.max_lon());
        }
    }
    return box;
}

void draw_layer(uint16_t* buf, const MapViewport& vp, const Layer& layer,
                const VectorMap& map, const QBox& view, uint16_t color, bool dashed) {
    const bool circle = (vp.projection == Projection::Azimuthal);
    // Rectangle the segment is pre-clipped to before rasterizing, so we never
    // run Bresenham across thousands of off-canvas pixels for far-away geometry.
    const double xmin = 0.0, ymin = 0.0;
    const double xmax = vp.width - 1.0, ymax = vp.height - 1.0;

    for (const auto& poly : layer.polylines) {
        if (!view.intersects(poly)) continue;
        double px0 = 0.0, py0 = 0.0;
        project_point(vp, map.dequant(poly.points[0]), px0, py0);
        for (size_t i = 1; i < poly.points.size(); ++i) {
            double px1 = 0.0, py1 = 0.0;
            project_point(vp, map.dequant(poly.points[i]), px1, py1);

            double cx0, cy0, cx1, cy1;
            if (geo::clip_segment(px0, py0, px1, py1, xmin, ymin, xmax, ymax,
                                  cx0, cy0, cx1, cy1)) {
                plot_line(buf, vp, static_cast<int>(std::lround(cx0)),
                          static_cast<int>(std::lround(cy0)),
                          static_cast<int>(std::lround(cx1)),
                          static_cast<int>(std::lround(cy1)), color, circle, dashed);
            }
            px0 = px1;
            py0 = py1;
        }
    }
}

// Even-odd scanline fill of a projected closed ring. Vertices may fall well off
// canvas; we only scan rows inside [0,height) and clip spans to [0,width).
void fill_polygon(uint16_t* buf, const MapViewport& vp, const VectorMap& map,
                  const Polyline& ring, uint16_t color) {
    const size_t n = ring.points.size();
    if (n < 3) return;

    std::vector<double> xs(n), ys(n);
    double ymin_d = 1e18, ymax_d = -1e18;
    for (size_t i = 0; i < n; ++i) {
        project_point(vp, map.dequant(ring.points[i]), xs[i], ys[i]);
        ymin_d = std::min(ymin_d, ys[i]);
        ymax_d = std::max(ymax_d, ys[i]);
    }
    const int y0 = std::max(0, static_cast<int>(std::floor(ymin_d)));
    const int y1 = std::min(vp.height - 1, static_cast<int>(std::ceil(ymax_d)));

    std::vector<double> xints;
    for (int y = y0; y <= y1; ++y) {
        const double yc = y + 0.5;
        xints.clear();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const double yi = ys[i], yj = ys[j];
            if ((yi <= yc && yj > yc) || (yj <= yc && yi > yc)) {
                xints.push_back(xs[i] + (yc - yi) / (yj - yi) * (xs[j] - xs[i]));
            }
        }
        std::sort(xints.begin(), xints.end());
        uint16_t* row = buf + static_cast<size_t>(y) * vp.width;
        for (size_t k = 0; k + 1 < xints.size(); k += 2) {
            const int xa = std::max(0, static_cast<int>(std::ceil(xints[k] - 0.5)));
            const int xb = std::min(vp.width - 1, static_cast<int>(std::floor(xints[k + 1] - 0.5)));
            for (int x = xa; x <= xb; ++x) row[x] = color;
        }
    }
}

} // namespace

void draw_base(uint16_t* buf, const MapViewport& vp,
               const VectorMap& map, const MapStyle& style) {
    if (!buf || !map.valid() || vp.width <= 0 || vp.height <= 0 || vp.radius_px <= 0) {
        return;
    }
    const QBox view = style.cull ? view_box(vp, map) : QBox{};
    // Map view: fill the whole canvas with the faint sea colour, then fill the
    // land polygons black on top. Only for the (rectangular) Mercator view — the
    // azimuthal radar keeps its black background and just gets the line layers.
    if (vp.projection == Projection::Mercator) {
        if (const Layer* land = map.layer(LayerId::Land)) {
            std::fill(buf, buf + static_cast<size_t>(vp.width) * vp.height, style.sea_color);
            for (const auto& ring : land->polylines) {
                if (view.intersects(ring)) fill_polygon(buf, vp, map, ring, style.land_color);
            }
        }
    }
    // Coast first, borders on top so a border line wins where they overlap.
    if (const Layer* coast = map.layer(LayerId::Coast)) {
        draw_layer(buf, vp, *coast, map, view, style.coast_color, false);
    }
    if (const Layer* border = map.layer(LayerId::Border)) {
        draw_layer(buf, vp, *border, map, view, style.border_color, style.border_dashed);
    }
}

namespace {

bool same_view(const MapViewport& a, const MapViewport& b) {
    return a.width == b.width && a.height == b.height && a.cx == b.cx && a.cy == b.cy &&
           a.radius_px == b.radius_px && a.home.lat == b.home.lat && a.home.lon == b.home.lon &&
           a.range_nm == b.range_nm && a.projection == b.projection;
}

} // namespace

void BaseMapCache::draw(uint16_t* buf, const MapViewport& vp, const VectorMap& map,
                        const MapStyle& style) {
    if (!buf || vp.width <= 0 || vp.height <= 0) return;
    const size_t n = static_cast<size_t>(vp.width) * vp.height;
    if (valid_ && map_ == &map && same_view(last_, vp) && last_style_ == style &&
        pixels_.size() == n) {
        std::copy(pixels_.begin(), pixels_.end(), buf);
        return;
    }
    draw_base(buf, vp, map, style);
    pixels_.assign(buf, buf + n);
    last_ = vp;
    last_style_ = style;
    map_ = &map;
    valid_ = true;
}

} // namespace toolkit::map
