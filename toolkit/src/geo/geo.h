/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace toolkit::geo {

struct LatLon {
    double lat{0.0};
    double lon{0.0};
};

// Great-circle distance between two points, in nautical miles (haversine).
double range_nm(LatLon home, LatLon p);

// Initial bearing from `home` to `p`, normalized to [0, 359] degrees (0 = N).
double bearing_deg(LatLon home, LatLon p);

// Project `p` onto a north-up PPI plot centred on `home`: maps (bearing, range)
// to a screen offset from the centre (dx positive right, dy positive down), for
// a plot of `radius_px` whose outer ring is `range_rings_nm_max` NM. Returns
// false (and leaves out_dx/out_dy untouched) if either input is invalid or the
// point lies beyond the maximum range. Bearings are clamped to 0..359.
bool project(LatLon home,
             LatLon p,
             double range_rings_nm_max,
             int radius_px,
             int& out_dx,
             int& out_dy);

// Project `p` onto a north-up conformal (Web-Mercator) map centred on `home`:
// maps (lat, lon) to a screen offset from the centre (dx positive right, dy
// positive down), scaled so that `span_nm` ground nautical miles (measured at
// the centre latitude) span `radius_px` vertically. Unlike project() this does
// NOT cull out-of-range points — it always returns a valid offset (which may
// fall outside the viewport) so the caller can clip line segments at the edge.
// Returns false (leaving outputs untouched) only on invalid input. The map is
// conformal at the centre, so a square canvas keeps the correct aspect ratio.
bool project_mercator(LatLon home,
                      LatLon p,
                      double span_nm,
                      int radius_px,
                      int& out_dx,
                      int& out_dy);

// Clip the segment (x0,y0)-(x1,y1) to the axis-aligned rectangle
// [xmin,xmax] x [ymin,ymax] (Liang-Barsky, pixel space). On success writes the
// clipped endpoints and returns true; returns false if the segment lies wholly
// outside the rectangle.
bool clip_segment(double x0, double y0, double x1, double y1,
                  double xmin, double ymin, double xmax, double ymax,
                  double& cx0, double& cy0, double& cx1, double& cy1);

} // namespace toolkit::geo
