/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "geo.h"

#include <cstdint>
#include <string>
#include <vector>

namespace toolkit::map {

// Layer ids in the RMAP binary (must match tools/mapdata/build_mapdata.py).
enum class LayerId : uint8_t {
    Coast   = 0,
    Border  = 1,
    Water   = 2,
    Contour = 3,
};

// A quantized point: 0..65535 across the dataset bounding box (x=lon, y=lat).
struct QPoint {
    uint16_t x{0};
    uint16_t y{0};
};

struct Polyline {
    std::vector<QPoint> points;
};

struct Layer {
    uint8_t id{0};
    std::vector<Polyline> polylines;
};

// A loaded vector map. Coordinates stay quantized in memory (4 bytes/point) and
// are dequantized on demand via dequant(); the bounding box drives the mapping.
class VectorMap {
public:
    // Load from an RMAP file. On any error (missing file, bad magic, truncated)
    // returns an empty map with valid()==false — callers degrade gracefully.
    static VectorMap load(const std::string& path);

    bool valid() const { return valid_; }
    const std::vector<Layer>& layers() const { return layers_; }
    const Layer* layer(LayerId id) const;

    geo::LatLon dequant(QPoint q) const {
        return {min_lat_ + (static_cast<double>(q.y) / 65535.0) * (max_lat_ - min_lat_),
                min_lon_ + (static_cast<double>(q.x) / 65535.0) * (max_lon_ - min_lon_)};
    }

    double min_lat() const { return min_lat_; }
    double min_lon() const { return min_lon_; }
    double max_lat() const { return max_lat_; }
    double max_lon() const { return max_lon_; }

private:
    bool valid_ = false;
    double min_lat_ = 0.0, min_lon_ = 0.0, max_lat_ = 0.0, max_lon_ = 0.0;
    std::vector<Layer> layers_;
};

} // namespace toolkit::map
