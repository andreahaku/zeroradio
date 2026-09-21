# Vector base map — design

The ADS-B, AIS and Meshtastic apps draw their entities over an offline vector base map: land,
coastlines and national borders from Natural Earth. The map runs the same on the SDL desktop
simulator and on the CardputerZero (320×170, RGB565). Each app toggles between a radar view and a
map view with a NavBar key and a persisted "Map view" Settings row.

## Why vector, not raster

The radar uses `toolkit::geo::project()`, an **azimuthal-equidistant** projection centred on the
home position (range in NM + bearing → screen offset, north-up). Raster tiles are Web Mercator. They
would need reprojection pixel by pixel, an HTTP client and a PNG decoder, and the device build has
none of these.

Vector geometry goes through the *same* projection as the entities, so it lines up with no extra
work, and the palette is our own choice of colours. The data ships in the package, so the map needs
no network.

## One dataset, two projections

A single dataset feeds both views. Only the projection formula differs.

| View | Projection | Notes |
|------|-----------|-------|
| **Radar** | `geo::project()` azimuthal | range/bearing from home, north-up, clipped to the range circle |
| **Map** | `geo::project_mercator()` | standard Mercator, clipped to the canvas rectangle |

The two views use **two fixed-size canvases**, and the app shows one or the other. Resizing an
`lv_canvas` at runtime crashes the SDL/Mesa flush, so the apps never do it.

| App | Radar canvas | Map canvas |
| --- | --- | --- |
| ADS-B, AIS | 120×120 | 320×120 |
| Meshtastic | 106×106 | 212×106 |

### Layers

1. **Land** — closed polygons, filled with an even-odd scanline fill. Drawn in the map view only:
   the radar keeps its plain background and gets the line layers.
2. **Coastline** — always drawn.
3. **National borders** — thin dashed lines, drawn over the coastline.

The format also reserves layer ids for water and contour lines. No dataset contains them yet.

### Palettes

`MapStyle` holds the colours. The default is dark: black land over a faint blue-grey sea, a faint
coastline and dim dashed borders, so the entity dots stay the brightest thing on screen.
`MapStyle::day()` is the Light theme palette: pale sea, near-white land and dark lines, readable in
direct sunlight. The ADS-B and AIS scopes pick it when the Light theme is on.

## Components

```
toolkit/src/geo/geo.h/.cpp            project(), project_mercator(), clip_segment()
toolkit/src/map/vector_map.h/.cpp     VectorMap: loads an .rmap file, per-polyline quantised bbox
toolkit/src/map/map_renderer.h/.cpp   draw_base(), BaseMapCache, MapStyle
assets/mapdata/world.rmap             the bundled dataset
tools/mapdata/build_mapdata.py        offline generator (never runs on the device)
toolkit/test/map_render_test.cpp      culled vs unculled output, pixel for pixel
```

Each app keeps its own entity plotting and calls the shared renderer for the layer underneath.

## Binary format (`.rmap`)

A small little-endian blob, not GeoJSON:

- Header: magic `RMAP`, version 1, the bounding box as four doubles (min lat, min lon, max lat,
  max lon), the layer count.
- Per layer: layer id, polyline count, then each polyline as a point count followed by `uint16`
  x/y pairs.
- Coordinates are **quantised to `uint16`** across the bounding box: 4 bytes per point.
- `build_mapdata.py` simplifies every polyline with **Douglas-Peucker** at build time.

`VectorMap` keeps the points quantised in memory and dequantises them on demand. The loader also
computes each polyline's quantised bounding box, which the renderer uses for culling.

## The bundled dataset

`assets/mapdata/world.rmap` is Natural Earth **10m**, worldwide, simplified at a tolerance of
**0.005°**: 2.0 MB and 492,103 points (land 231,584, coastline 219,200, borders 41,319). The
tolerance sits about at the precision limit of the 16-bit quantisation over the whole world.

Regenerate it with:

```bash
python3 tools/mapdata/build_mapdata.py --scale 10m --tol 0.005
```

The script uses only the Python standard library. It downloads the Natural Earth GeoJSON once into
a cache directory (`tools/mapdata/cache/`, git-ignored, or `$MAPDATA_CACHE`), so later runs work
offline. `--bbox lon_min,lat_min,lon_max,lat_max` builds a regional cut and `--out` sets the output
path. Without flags the script builds a 50m world map at 0.02°, which is not the shipped dataset.

Natural Earth is public domain.

## Performance

The renderer does two things to stay cheap on the CM0:

- **Culling.** `draw_base()` computes the part of the map that can reach the canvas as a quantised
  box, with a 20 % margin. It skips every polyline whose bounding box misses it, without projecting
  a single point. Near the poles or across the antimeridian it culls on latitude only.
  `map_render_test` checks that culled and unculled output match pixel for pixel on six views,
  including the antimeridian and the Arctic.
- **Caching.** The base map only changes with home, range, canvas size, projection or palette.
  `BaseMapCache` keeps the pixels of the last render and replays them while none of these changed.
  A steady view then costs one copy of the canvas buffer per 300 ms tick. The caller clears the
  canvas to the same background before every call, because a cache hit replays the whole buffer.

Measured on the CM0 with the current dataset, a culled draw costs about 8-19 ms for a radar view and
about 40-100 ms for a Mercator view with the land fill. A cached tick costs only the copy.

## Future options

- **Contour lines (relief).** DEM-derived isolines (GEBCO/SRTM through `gdal_contour`) are the
  heaviest layer. They would be regional, with few intervals, and off by default.
- **Graticule and distance rings** generated on the fly, with no stored data.
- **Per-layer toggles** for coast, borders and contours.
- **Raster background** (online or pre-downloaded tiles) as a user option. It needs an HTTP client,
  a PNG decoder and Mercator tile compositing, so it would come to the desktop simulator first. The
  map view already uses Mercator, so a raster layer can go under it.
