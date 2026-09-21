# radio_toolkit

`radio_toolkit` is the static library every ZeroRadio app builds on. It provides the LVGL app shell,
the reactive MVVM layer, the widgets, the raster and map renderers, the data sources and the decoder
lifecycle. An app adds a **parser, a field mapping and a NavProvider** on top of it. See
[`docs/architecture.md`](../docs/architecture.md) for the design and data flow.

## Modules (`toolkit/src/`)

| Module | Contents |
| --- | --- |
| `app/run_app` | `run_app()`: `lv_init`, display, theme and key handlers, then builds the screen and runs the loop. It opens the help page on H and returns `ShellViewModel::exit_code()`. |
| `app/shell_viewmodel` | `ShellViewModel`: generic shell subjects (dark mode, toolbar page, quit, title, nav-refresh) and the per-app hooks. `on_tab()`, `on_up()` and `on_down()` are virtual no-ops that an app overrides. `set_help_doc()` names the app's help page. `request_home()` quits with `kExitHome` (the 3 s ESC hold). `request_handoff()` asks for a sibling app (see `app/handoff`). |
| `app/handoff` | `run_handoff()`: after `run_app()` returns, `exec`s the sibling app that `request_handoff()` named, in place (same PID), with the extra environment. `find_sibling()` finds the binary next to this executable, else in the dev build tree. Scanner uses it for "Open in SDR", and SDR and Scanner for TAB. |
| `app/docs` | `read_doc(path)`: reads a help page, `CHANGELOG.md` or `CREDITS.md` by its repository-relative path. The desktop build reads the source tree. The device build reads `/usr/share/zeroradio`. |
| `app/` (other) | `NavProvider` (the interface an app implements to populate the NavBar), `AssetManager`, `persisted_state` (atomic save/restore of app settings under `~/.config/zeroradio/<app>/`). |
| `reactive/` | `subjects` / `bindings`: the LVGL-subject one-way observe layer. The UI never reads state directly. |
| `view/` | `theme`, `ui_const.h` (Phosphor icon codepoints), `raster`: the RGB565 primitives (`plot_disc` / `plot_ring` / `plot_triangle` / `plot_line`) and the waterfall `colormap_rgb565`, used by every scope and waterfall in the suite. |
| `view/screens/` | `base_screen`: a screen base parameterised on `ShellViewModel&` + `NavProvider&`. |
| `view/widgets/` | `NavBar` (the 5-key bottom toolbar), `IconButton`, `TitleBar`, `base_widget`, and the widgets below. |
| `view/widgets/text_viewer` | `TextViewer`: a full-screen reader for help and About pages. F/X scroll, Z/C change page, ESC closes. `render_markdown()` renders the Markdown subset those pages use (`#`/`##` headings, `-` bullets, blank lines). |
| `view/widgets/location_dialog` | `LocationDialog`: a modal that sets the shared position. The user types a city or coordinates, or picks the GPS row, which shows the live satellite count and then the fix. |
| `view/widgets/battery_badge` | `BatteryBadge`: battery icon and percent for an app header. It refreshes every 10 s, turns red below 20 %, marks charging, and hides itself where there is no battery. |
| `map/` | The vector base map. `vector_map` loads the bundled `.rmap` files and stores a quantised bounding box per polyline. `map_renderer` provides `draw_base` (land fill, coastline, national borders onto an RGB565 canvas), which skips polylines whose box misses the view. `BaseMapCache` replays the last render while the view is unchanged. `MapStyle::day()` is the palette for the Light theme. Format and pipeline: [`docs/mapdata-design.md`](../docs/mapdata-design.md). |
| `model/` | `entity_store`: a thread-safe table of tracked entities (sparse-merge upsert, snapshot, TTL sweep) behind every aircraft, vessel and node list. |
| `net/` | `file_json_source` polls a file on a background thread and hands its contents to a callback (the ADS-B `aircraft.json`). `nmea_net_source` receives NMEA lines over UDP/TCP with multi-fragment reassembly (AIS). |
| `geo/` | Haversine `range_nm` / `bearing_deg`, the north-up azimuthal `project()` for the radar, `project_mercator()` + `clip_segment()` for the map. |
| `geo/location` | The position the suite shares: `load()` / `save()` (`~/.config/zeroradio/location`), `parse_coords()`, `CityIndex` (offline search over `assets/geodata/cities.tsv`), and `GnssReader`. `GnssReader` tries `$ZERORADIO_GPS_DEVICE`, then a USB GPS (`/dev/ttyACM*`, `/dev/ttyUSB*`), then the Cap LoRa-1262-GPS shield on the HAT port, powered from the HAT 5 V rail. |
| `platform/linux_input` | Key routing from the keyboard to LVGL: NavBar keys 4-8, the ESC handler (short press quits, a 0.5 s hold shows a hint, a 3 s hold goes home), TAB, H, and the up/down arrow handler (arrows or F/X). `set_key_capture()` hands every key to a dialog. Its `text` flag turns F/X/Z/C into letters for free-text entry. |
| `platform/subprocess` | `Subprocess`: fork/exec of a child tool with a non-blocking stdout pipe. Every child gets `PR_SET_PDEATHSIG`, so it dies with the app. ISM (`rtl_433`) and Scanner (`rtl_power` / `hackrf_sweep`) use it directly. |
| `platform/child_service` | `ChildService`: keeps a decoder running for the app's lifetime. It drains the tool's stdout, restarts it after a fixed pause when it exits, and stops it in the destructor. `find_tool()` prefers a binary bundled next to the executable, else uses `PATH`. SDR (`rtl_tcp`), ADS-B (`readsb`) and AIS (`AIS-catcher`) use it. |
| `platform/battery` | `read_battery()`: level and charging state from the first battery under `/sys/class/power_supply` (the [CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero)'s BQ27220 gauge). |
| `platform/remote_fb` | The headless streaming display and input driver (see [`tools/remote-fb/README.md`](../tools/remote-fb/README.md)). |
| `logger/` | A small file/stderr logger. |
| `config/` | `app_config.h` (config-file path helper) and the LVGL configs (`lv_conf_desktop.h` for the SDL simulator, `lv_conf_cm0.h` for the device). |

Tests live in `toolkit/test/`: `waterfall_scroll_test`, `map_render_test` (culling must not change a
pixel) and `location_test`.

## Conventions

- **One-way data flow**: sources write into a store under a mutex on a reader thread. An LVGL timer
  snapshots the store and draws. Actions flow key → `NavProvider::nav_activate` → view-model →
  subject → UI.
- **Decoders are pure libraries** (no LVGL, no sockets), so they link standalone into the frozen
  parity tests. See the Tests section of [`docs/architecture.md`](../docs/architecture.md).
- **One decoder process per app**, started with `ChildService` or `Subprocess` and stopped when the
  app exits.
- **No per-app raster or map code**: scopes draw with `view::raster`, maps with `map/`. A new app
  gets both.
