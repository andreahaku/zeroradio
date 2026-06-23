#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
# SPDX-License-Identifier: MIT
"""Build a compact binary vector-map asset (.rmap) for the radio-apps map layer.

Pure standard-library Python (no GDAL/geopandas/pyshp). Pulls Natural Earth
coastline + national-boundary GeoJSON, clips to a bounding box, simplifies with
Douglas-Peucker, quantizes coordinates to uint16, and writes the `RMAP` binary
format consumed by toolkit::geo::VectorMap.

Run from anywhere:
    python3 tools/mapdata/build_mapdata.py            # default: Adriatic/Italy
    python3 tools/mapdata/build_mapdata.py --bbox 6.5,39,19.5,47 --out assets/mapdata/adriatic.rmap

Natural Earth is public domain. Source data is cached under a scratch dir so
re-runs are offline after the first fetch. This tool never runs on-device.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
import urllib.request

# --- RMAP binary format ------------------------------------------------------
# Little-endian throughout.
#
#   magic     : 4 bytes  b"RMAP"
#   version   : uint8    = 1
#   flags     : uint8    = 0 (reserved)
#   reserved  : uint16   = 0
#   min_lat,min_lon,max_lat,max_lon : float64 x4   (dequant box)
#   layer_count : uint16
#   per layer:
#     layer_id   : uint8   (0=coast, 1=border, 2=water, 3=contour)
#     layer_flags: uint8   (reserved)
#     reserved   : uint16
#     polyline_count : uint32
#     per polyline:
#       point_count : uint16
#       per point: qx:uint16, qy:uint16
#         lon = min_lon + qx/65535 * (max_lon-min_lon)
#         lat = min_lat + qy/65535 * (max_lat-min_lat)
MAGIC = b"RMAP"
VERSION = 1
QMAX = 65535

LAYER_COAST = 0
LAYER_BORDER = 1

NE_BASE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson"
SOURCES = {
    "coast": {
        "10m": "ne_10m_coastline.geojson",
        "50m": "ne_50m_coastline.geojson",
        "layer": LAYER_COAST,
    },
    "border": {
        "10m": "ne_10m_admin_0_boundary_lines_land.geojson",
        "50m": "ne_50m_admin_0_boundary_lines_land.geojson",
        "layer": LAYER_BORDER,
    },
}


def cache_dir() -> str:
    base = os.environ.get("MAPDATA_CACHE")
    if base:
        return base
    scratch = (
        "<scratch-dir>"
        "CardputerZero-cardputer-radio/05c46d00-3345-4358-ae04-e195d391ea36/"
        "scratchpad/mapdata-cache"
    )
    return scratch if os.path.isdir(os.path.dirname(scratch)) else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "cache"
    )


def fetch_geojson(filename: str) -> dict:
    cdir = cache_dir()
    os.makedirs(cdir, exist_ok=True)
    path = os.path.join(cdir, filename)
    if not os.path.exists(path):
        url = f"{NE_BASE}/{filename}"
        print(f"  fetching {url}", file=sys.stderr)
        urllib.request.urlretrieve(url, path)
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def iter_linestrings(geojson: dict):
    """Yield each LineString as a list of (lon, lat) tuples."""
    for feat in geojson.get("features", []):
        geom = feat.get("geometry") or {}
        gtype = geom.get("type")
        coords = geom.get("coordinates") or []
        if gtype == "LineString":
            yield [(float(x), float(y)) for x, y in coords]
        elif gtype == "MultiLineString":
            for line in coords:
                yield [(float(x), float(y)) for x, y in line]


# --- Liang-Barsky segment clip against an axis-aligned bbox ------------------
def clip_segment(x0, y0, x1, y1, xmin, ymin, xmax, ymax):
    dx = x1 - x0
    dy = y1 - y0
    p = [-dx, dx, -dy, dy]
    q = [x0 - xmin, xmax - x0, y0 - ymin, ymax - y0]
    u0, u1 = 0.0, 1.0
    for pi, qi in zip(p, q):
        if pi == 0:
            if qi < 0:
                return None  # parallel and outside
        else:
            t = qi / pi
            if pi < 0:
                if t > u1:
                    return None
                if t > u0:
                    u0 = t
            else:
                if t < u0:
                    return None
                if t < u1:
                    u1 = t
    return (x0 + u0 * dx, y0 + u0 * dy, x0 + u1 * dx, y0 + u1 * dy)


def clip_polyline(points, bbox):
    """Clip a polyline to bbox, returning a list of sub-polylines."""
    xmin, ymin, xmax, ymax = bbox
    runs = []
    cur = []
    for i in range(len(points) - 1):
        x0, y0 = points[i]
        x1, y1 = points[i + 1]
        seg = clip_segment(x0, y0, x1, y1, xmin, ymin, xmax, ymax)
        if seg is None:
            if cur:
                runs.append(cur)
                cur = []
            continue
        cx0, cy0, cx1, cy1 = seg
        if not cur:
            cur = [(cx0, cy0)]
        elif cur[-1] != (cx0, cy0):
            # Segment re-entered the box at a new point: break the run.
            runs.append(cur)
            cur = [(cx0, cy0)]
        cur.append((cx1, cy1))
    if cur:
        runs.append(cur)
    return [r for r in runs if len(r) >= 2]


# --- Douglas-Peucker simplification (planar, fine for a small region) --------
def _perp_dist(p, a, b):
    (px, py), (ax, ay), (bx, by) = p, a, b
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def simplify(points, tol):
    if len(points) < 3 or tol <= 0:
        return points
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        lo, hi = stack.pop()
        dmax, idx = 0.0, -1
        for i in range(lo + 1, hi):
            d = _perp_dist(points[i], points[lo], points[hi])
            if d > dmax:
                dmax, idx = d, i
        if idx != -1 and dmax > tol:
            keep[idx] = True
            stack.append((lo, idx))
            stack.append((idx, hi))
    return [p for p, k in zip(points, keep) if k]


def quantize(lon, lat, bbox):
    xmin, ymin, xmax, ymax = bbox
    qx = round((lon - xmin) / (xmax - xmin) * QMAX) if xmax > xmin else 0
    qy = round((lat - ymin) / (ymax - ymin) * QMAX) if ymax > ymin else 0
    return max(0, min(QMAX, qx)), max(0, min(QMAX, qy))


def build_layer(kind, scale, bbox, tol):
    src = SOURCES[kind]
    gj = fetch_geojson(src[scale])
    polylines = []
    for line in iter_linestrings(gj):
        for run in clip_polyline(line, bbox):
            run = simplify(run, tol)
            if len(run) >= 2:
                polylines.append([quantize(lon, lat, bbox) for lon, lat in run])
    return src["layer"], polylines


def write_rmap(path, bbox, layers):
    xmin, ymin, xmax, ymax = bbox
    buf = bytearray()
    buf += MAGIC
    buf += struct.pack("<BBH", VERSION, 0, 0)
    # header bbox stored as lat/lon pairs (min_lat,min_lon,max_lat,max_lon)
    buf += struct.pack("<4d", ymin, xmin, ymax, xmax)
    buf += struct.pack("<H", len(layers))
    total_pts = 0
    for layer_id, polylines in layers:
        buf += struct.pack("<BBH", layer_id, 0, 0)
        buf += struct.pack("<I", len(polylines))
        for poly in polylines:
            buf += struct.pack("<H", len(poly))
            for qx, qy in poly:
                buf += struct.pack("<HH", qx, qy)
                total_pts += 1
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(buf)
    return len(buf), total_pts


def main():
    ap = argparse.ArgumentParser(description="Build a compact .rmap vector-map asset.")
    ap.add_argument("--bbox", default="6.5,39.0,19.5,47.0",
                    help="lon_min,lat_min,lon_max,lat_max (default: Italy/Adriatic)")
    ap.add_argument("--scale", choices=["10m", "50m"], default="10m")
    ap.add_argument("--tol", type=float, default=0.004,
                    help="Douglas-Peucker tolerance in degrees (default 0.004 ~ 400m)")
    ap.add_argument("--out", default=None, help="output .rmap path")
    args = ap.parse_args()

    lon_min, lat_min, lon_max, lat_max = (float(v) for v in args.bbox.split(","))
    bbox = (lon_min, lat_min, lon_max, lat_max)

    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    out = args.out or os.path.join(repo, "assets", "mapdata", "adriatic.rmap")

    print(f"bbox={bbox} scale={args.scale} tol={args.tol}", file=sys.stderr)
    layers = []
    for kind in ("coast", "border"):
        layer_id, polys = build_layer(kind, args.scale, bbox, args.tol)
        npts = sum(len(p) for p in polys)
        print(f"  {kind:6s}: {len(polys):4d} polylines, {npts:6d} points", file=sys.stderr)
        layers.append((layer_id, polys))

    size, total = write_rmap(out, bbox, layers)
    print(f"wrote {out}", file=sys.stderr)
    print(f"  {size} bytes, {total} points total", file=sys.stderr)


if __name__ == "__main__":
    main()
