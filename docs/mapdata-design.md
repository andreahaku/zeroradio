# Vector map background — design

Status: **Phases 1–3 implemented** — the shared vector Mercator base map ships in the
**Meshtastic, ADS-B and AIS** map views (radar ↔ map toggle in each).
Scope: shared background map layer for the geo-radar apps.

## Phase 1 — what shipped

- Compact `.rmap` binary format + `tools/mapdata/build_mapdata.py` (Natural Earth
  land + coast + national borders, Douglas-Peucker, quantized to uint16). Now ships
  a **worldwide** dataset (`assets/mapdata/world.rmap`, 50m, ~323 KB) so the map
  works anywhere — both apps load it; regenerate a high-detail regional cut with
  `--bbox <lon,lat,lon,lat> --scale 10m`. (Was Italy/Adriatic-only, ~26 KB.)
- `toolkit::geo::project_mercator()` + `clip_segment()`.
- `toolkit/src/map/`: `VectorMap` loader + shared `map_renderer` base layer.
- Meshtastic Map view: radar (azimuthal PPI) ↔ Mercator map toggle (key 8 +
  "Map view" settings row, persisted). The Mercator canvas is **twice as wide**
  (212×106) than the square radar (106²).
- Side short-name columns: selected node renders **inverted** (dot colour fill +
  black text), self renders **bold**, right column right-aligned.

Implementation note: the two views use **two separate fixed-size canvases**, not
one resized at runtime — resizing an `lv_canvas` mid-run crashes the SDL/Mesa
flush (see memory `lvgl-canvas-runtime-resize-crash`).

Open polish: the radar/map toggle icon is provisional (squares-2x2); the coastline
colour renders lighter than intended.

## Goal

Add an optional background "map" behind the entity dots in the PPI scope, so planes /
nodes / ships are shown against coastlines and borders instead of empty black. Must be
**dark** (UI-consistent), **light on data**, and run **identically on the SDL desktop sim
and the CardputerZero device** (320×170, RGB565).

## Key constraint that shaped the design

`toolkit::geo::project()` is an **azimuthal-equidistant** projection centred on `home`
(range in NM + bearing → screen offset, north-up). It is **not** Web Mercator. Raster
slippy tiles / static map images are Mercator and would have to be reprojected
pixel-by-pixel to sit under this scope — plus they need an HTTP client and PNG decode,
**neither of which exists on the device build** (`asset_manager` is font-only; libpng/jpeg
are linked only in the desktop sim).

Drawing the map **vectorially** sidesteps all of this: map vertices go through the *same*
projection as the entities, so the geometry is correct for free, and "dark" is just our
choice of line colour.

## Architecture: one dataset, two projections, two views

A single compact vector dataset feeds **both** views; only the projection formula differs.

| View | Projection | Notes |
|------|-----------|-------|
| **Radar** | `geo::project()` azimuthal (existing) | range/bearing from `home`, north-up |
| **Map** | new `geo::project_mercator()` | `x = f(lon)`, `y = f(lat)`; standard Mercator |

The user toggles between them (per-app setting, persisted). No duplicated data: coastlines,
borders and contours are stored once and re-projected on demand.

### Layers (each independently toggleable)

1. **Coastlines + water** — Natural Earth, always on, the cheapest "sense of place".
2. **National borders** — Natural Earth, thin dashed lines.
3. **Contour lines (relief)** — DEM-derived isolines. **Heaviest layer**, so it is
   **regional + optional** (off by default); see "Contours" below.
4. **Graticule / distance rings** — generated on the fly, zero stored data (free, optional).

## Components (all new, in the shared toolkit — no per-app duplication)

```
toolkit/src/geo/geo.h/.cpp          + project_mercator(), polyline clip helpers
toolkit/src/geo/vector_map.h/.cpp   loader for the compact binary map format
toolkit/src/map/map_renderer.h/.cpp draws a base-map layer onto an RGB565 canvas buffer,
                                    given (dataset, projection, viewport). Apps blit their
                                    entity dots on top — see "Shared renderer" below.
assets/mapdata/<region>.rmap        bundled compact dataset(s)
tools/mapdata/build_mapdata.py      offline generator (NOT in the runtime path)
```

### Compact binary format (the real lever for "less data")

Not GeoJSON. A small binary blob:

- Header: layer table (coast / border / contour), bounding box, coordinate scale.
- Polylines stored as **quantized integers** (e.g. lat/lon in fixed-point int16/int32 over
  the region's bbox), not doubles → ~4–8× smaller than text GeoJSON.
- Vertices **decimated with Douglas–Peucker** at build time to the resolution that 106 px
  can actually show.
- Result: a regional dataset (coast+border) fits in **a few KB**; low-res world in tens of KB.

Bundled into `assets/` → **zero runtime networking**, works fully offline, identical on device.

### Offline data pipeline (`tools/mapdata/`)

- Coast/water/borders: **Natural Earth** (public domain) at 1:50m / 1:10m, subset to bbox.
- Contours: **GEBCO/SRTM DEM** → `gdal_contour` at a few intervals (e.g. every 200–500 m) →
  simplified → quantized.
- Run by us, output committed under `assets/mapdata/`. Never executed on device/runtime.

## Shared renderer (light, surgical refactor)

Today ADS-B (`adsb_screen.cpp:686 render_scope()`) and Meshtastic
(`meshtastic_screen.cpp:681 update_map()`) each re-implement the ring/disc drawing inline.
With AIS coming, that's about to be tripled. We extract **only the base-map drawing** into
`toolkit/src/map/map_renderer` (rings + projected polylines onto an RGB565 buffer). The
existing entity-plotting code in each app stays put — we add a base layer *underneath*, we
do not rewrite the working radars. Strictly additive.

## Performance

- Base-map layer is re-projected **only on view change** (home / range / projection /
  layer toggle), cached in its own RGB565 buffer, and **blitted under the entities every
  300 ms tick** — so we never re-project coastlines per frame.
- Off-screen polylines are clipped before projecting.
- Quantization + Douglas–Peucker keep the projected segment count low enough for ARM.

## Contours — honest note

The user asked for contour (relief) lines. They are genuinely the heaviest option and are
in tension with "reduce the data" on a 106 px disc. Mitigation: keep them **regional**
(only the area around `home`), **few intervals**, aggressively simplified, **off by default**
behind a toggle. Worldwide contours are explicitly out of scope.

## Raster map — future option (architected for, not built now)

Per the decision, a raster background (e.g. CARTO dark_matter tiles) lands later as a **user
option**: "vector (offline)" vs "raster (online / pre-downloaded)". It needs an HTTP client +
PNG decode + Mercator tile compositing, so it is **desktop-sim-first** until the device build
gains codecs. The map view is built Mercator-native now precisely so a raster layer can drop
in under the same projection later. No raster code in phase 1.

## Phased plan

- **Phase 1 — foundation (desktop sim, Meshtastic first)** — ✅ shipped:
  binary format + `build_mapdata.py` + `vector_map` loader + `project_mercator()` +
  `map_renderer` base layer + radar/map toggle setting.
- **Phase 2 — roll-out (ADS-B)** — ✅ shipped: the shared renderer is wired into the ADS-B
  Radar screen (key `8` toggles). Layer toggles (coast/border/contour) and the regional
  contour dataset remain future work.
- **Phase 3 — AIS** — ✅ shipped: the AIS app got the map "for free" via the shared renderer.
- **Phase 4 — later:** optional online raster layer (desktop-first).

## Decisions taken (was "Open questions for go")

1. Phase 1 started on **Meshtastic** (it already had a Map view + persisted settings).
2. Toggle exposure: **both** — key `8` in the view and a persisted "Map view" Settings row.
3. First regional dataset: **Italy / Adriatic**; later replaced as the default by the
   worldwide 50m dataset (`world.rmap`), with regional cuts still supported via `--bbox`.
