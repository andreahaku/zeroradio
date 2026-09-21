/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#include "vector_map.h"

#include <algorithm>

#include <cstring>
#include <fstream>

namespace toolkit::map {
namespace {

// Little-endian cursor over an in-memory buffer with bounds checking. Any read
// past the end flips `ok` so a truncated/corrupt file fails the whole load
// rather than producing garbage geometry.
struct Reader {
    const uint8_t* p;
    size_t n;
    size_t off = 0;
    bool ok = true;

    bool need(size_t bytes) {
        if (!ok || off + bytes > n) {
            ok = false;
            return false;
        }
        return true;
    }
    uint8_t u8() { return need(1) ? p[off++] : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = static_cast<uint16_t>(p[off]) | (static_cast<uint16_t>(p[off + 1]) << 8);
        off += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = static_cast<uint32_t>(p[off]) | (static_cast<uint32_t>(p[off + 1]) << 8) |
                     (static_cast<uint32_t>(p[off + 2]) << 16) |
                     (static_cast<uint32_t>(p[off + 3]) << 24);
        off += 4;
        return v;
    }
    double f64() {
        if (!need(8)) return 0.0;
        double v;
        std::memcpy(&v, p + off, 8); // file is little-endian; targets are too
        off += 8;
        return v;
    }
};

} // namespace

const Layer* VectorMap::layer(LayerId id) const {
    for (const auto& l : layers_) {
        if (l.id == static_cast<uint8_t>(id)) {
            return &l;
        }
    }
    return nullptr;
}

VectorMap VectorMap::load(const std::string& path) {
    VectorMap map;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return map; // missing file -> invalid, caller degrades to no base map
    }
    const std::streamsize size = f.tellg();
    if (size <= 0) {
        return map;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(buf.data()), size)) {
        return map;
    }

    Reader r{buf.data(), buf.size()};
    if (buf.size() < 4 || std::memcmp(buf.data(), "RMAP", 4) != 0) {
        return map;
    }
    r.off = 4;
    const uint8_t version = r.u8();
    r.u8();  // flags
    r.u16(); // reserved
    if (version != 1) {
        return map;
    }

    map.min_lat_ = r.f64();
    map.min_lon_ = r.f64();
    map.max_lat_ = r.f64();
    map.max_lon_ = r.f64();
    const uint16_t layer_count = r.u16();

    map.layers_.reserve(layer_count);
    for (uint16_t li = 0; li < layer_count && r.ok; ++li) {
        Layer layer;
        layer.id = r.u8();
        r.u8();  // layer flags
        r.u16(); // reserved
        const uint32_t poly_count = r.u32();
        layer.polylines.reserve(poly_count);
        for (uint32_t pi = 0; pi < poly_count && r.ok; ++pi) {
            const uint16_t pt_count = r.u16();
            Polyline poly;
            poly.points.reserve(pt_count);
            for (uint16_t k = 0; k < pt_count && r.ok; ++k) {
                QPoint q;
                q.x = r.u16();
                q.y = r.u16();
                poly.points.push_back(q);
                poly.lo.x = std::min(poly.lo.x, q.x);
                poly.lo.y = std::min(poly.lo.y, q.y);
                poly.hi.x = std::max(poly.hi.x, q.x);
                poly.hi.y = std::max(poly.hi.y, q.y);
            }
            if (poly.points.size() >= 2) {
                layer.polylines.push_back(std::move(poly));
            }
        }
        map.layers_.push_back(std::move(layer));
    }

    map.valid_ = r.ok;
    return map;
}

} // namespace toolkit::map
