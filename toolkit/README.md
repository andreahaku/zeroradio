# radio_toolkit — the shared foundation

`radio_toolkit` is the static library every app in this monorepo builds on: the LVGL app shell,
the reactive MVVM layer, the shared widgets and raster/map renderers, and the resilient data
sources. An app is meant to be a thin **parser + field mapping + NavProvider** over this toolkit —
see [`docs/architecture.md`](../docs/architecture.md) for the design and data flow.

## Modules (`toolkit/src/`)

| Module | Contents |
| --- | --- |
| `app/` | `run_app()` (lifecycle: `lv_init` + display + theme + quit handler, then build the screen and run the loop), `ShellViewModel` (generic shell subjects: dark mode, toolbar page, quit, title, nav-refresh), `NavProvider` (the interface an app implements to populate the NavBar), `AssetManager`, `persisted_state` (atomic save/restore of app settings). |
| `reactive/` | `subjects` / `bindings` — the LVGL-subject one-way observe layer; the UI never reads state directly. |
| `view/` | `theme`, `ui_const.h` (Phosphor icon codepoints), `raster` — the shared RGB565 primitives (`plot_disc` / `plot_ring` / `plot_triangle` / `plot_line`) and the waterfall `colormap_rgb565`, used by every scope and waterfall in the suite. |
| `view/screens/` | `base_screen` — a screen base parameterized on `ShellViewModel&` + `NavProvider&`. |
| `view/widgets/` | `NavBar` (the 5-key bottom toolbar), `IconButton`, `TitleBar`, `base_widget`. |
| `map/` | The Mercator vector base map: `vector_map` (loads the compact bundled `.rmap` datasets) and `map_renderer` (`draw_base`: filled land, coastline, national borders onto an RGB565 canvas). Shared by the ADS-B / AIS / Meshtastic map screens. Format and pipeline: [`docs/mapdata-design.md`](../docs/mapdata-design.md). |
| `model/` | `entity_store` — a thread-safe table of tracked entities (sparse-merge upsert, snapshot, TTL sweep) behind every aircraft/vessel/node list. |
| `net/` | `file_json_source` — poll a file on a background thread and hand its contents to a callback (dump1090-style feeds); `nmea_net_source` — receive NMEA lines over UDP/TCP with multi-fragment reassembly (AIS). |
| `geo/` | Haversine `range_nm` / `bearing_deg`, the north-up azimuthal `project()` for the PPI radar, `project_mercator()` + `clip_segment()` for the map. |
| `platform/` | `linux_input` — key routing (physical keys and arrow keys → LVGL); `remote_fb` — the headless streaming display+input driver (see [`tools/remote-fb/README.md`](../tools/remote-fb/README.md)). |
| `logger/` | A small file/stderr logger. |
| `config/` | `app_config.h` (config-file path helper) and the LVGL configs (`lv_conf_desktop.h` for the SDL simulator, `lv_conf_cm0.h` for the device). |

## Conventions

- **One-way data flow**: sources write into a store under a mutex on a reader thread; an LVGL
  timer snapshots and draws. Actions flow key → `NavProvider::nav_activate` → viewmodel →
  subject → UI.
- **Decoders are pure libraries** (no LVGL, no sockets) so they link standalone into the frozen
  parity tests — see the Tests section of the root [`README.md`](../README.md).
- **No per-app raster/map duplication**: scopes draw with `view::raster`, maps with `map/` — new
  apps get both for free.
