# meshtastic — Meshtastic mesh client

A Meshtastic LoRa-mesh client for the CardputerZero, built on the shared `radio_toolkit`. It is a
**client of a local `meshtasticd` daemon** (Client API over `127.0.0.1:4403`), not a reimplementation of
the radio — see the design docs in [`../../../radio-apps/`](../../../radio-apps/) (`09`, `09a`, `09b`,
`09c`). UI follows the chosen dark theme (`09c`).

## Status

Working (verified on a `meshtasticd -s` bench / scripted peer, both presets host + cp0 aarch64):

- **Connection + handshake** — `MeshtasticClientSource` (`src/net/`): background TCP client, `0x94C3`
  framing, `want_config_id` → `config_complete_id`, resilient reconnect/backoff + heartbeat.
- **Nodes** — `NodeInfo` → `EntityStore`; the NODES view is a themed `lv_table` (SHORT/SNR/HOP/AGE) with
  a green cursor band (keys ▲/▼) and the self node in accent green. Title shows the live node count.
- **Chat receive** — `TEXT_MESSAGE_APP` → `MessageLog`; the CHATS view shows the feed (sender short name
  recoloured: self green, peers info-blue), filtered to the current conversation.
- **Chat send + compose** — the `8` (write) key enters a compose row (raw key capture via
  `platform::set_key_capture`); type, **`Enter` sends and returns**, `Esc` cancels. Sends route to the
  current conversation (channel broadcast or DM peer) and echo to our own feed.
- **ACK color-outline** — `ROUTING_APP` matched by `request_id` updates a sent message's state; the feed
  shows a delivery dot: amber (pending) → green (delivered) / red (failed).
- **Channel switch** — `FromRadio.channel` → `ChannelTable`; the feed/compose target is a *conversation*
  (a channel slot or a DM peer). Key `5` cycles the active channels by name (e.g. `#LongFast` → `#Private`)
  and the title shows the current one. The DM entry point (from a node) lands with Node detail.
- **Canned messages** — key `6` opens a numbered overlay of quick replies; pressing a digit sends that
  preset on the current conversation, `Esc` cancels. Reactions (`7`) are greyed (V2).
- **Map** — a north-up scope centred on the self node (or the mesh centroid when self has no fix), with
  two modes toggled by key `8` (also a "Map view" settings row, persisted): a **radar** (azimuthal PPI,
  km rings) and a **Mercator map** that draws a vector coastline/border background (Natural Earth, bundled
  as `assets/mapdata/*.rmap`) under the node dots — the map canvas is twice as wide as the square radar.
  Range auto-fits all positioned nodes with manual zoom on keys `5`/`6`; key `7` cycles the selection.
  Each peer gets a distinct colour shared by its dot and its **side-column** name; the selected node
  renders inverted (its colour as background, black text) and the self node bold. Title shows the count.

- **Node detail** — key `8` from the NODES list opens a full-field sub-screen: `SHORT · LONG · id`,
  `HW / ROLE`, `SNR / HOPS`, `BATT / VOLT`, `POS / DIST / BRG` (relative to the self node),
  `HEARD`. Key `5` from the detail starts a DM (opens CHATS filtered to that peer). Key `8` goes back.
  HW model and role decode from the NodeInfo `user.hw_model` / `user.role`; battery and voltage from
  `device_metrics`.

- **Tools** — a 4-item list (cursor `›` in green; V2 items greyed). Key `7` runs the selected tool
  inline (toggle output panel); key `8` closes it. V1 tools: *Mesh stats* (link state, node count,
  packets/sec + total) and *Packet log* (TEXT / NODEINFO / POS counters). *Traceroute* and
  *Telemetry req* are greyed V2 entries. Packet counters are incremented on the reader thread in
  the client source (`cnt_text`, `cnt_nodeinfo`, `cnt_pos`, `cnt_total`).

- **Settings** — a 2-column lv_table (name | value) with the same green cursor band as ADS-B
  settings. V1 items: Theme (Dark/Light cycle), Long name (text edit), Short name (text edit),
  Region (picker through standard region codes), Channel (read-only display from ChannelTable).
  Key `7` cycles pickers or opens a compose-style text editor (Enter commits, Esc cancels).
  Key `8` returns to Chats. Values persisted to `~/.config/cardputer_radio/meshtastic/settings`
  and restored on launch. Writing settings back to `meshtasticd` (AdminMessage) is V2.

Placeholders / pending: reactions (V2), Traceroute (V2), Telemetry req (V2),
settings → meshtasticd (AdminMessage, V2).
See `radio-apps/09b` (build order) and `09c` (per-screen design).

## Screenshots

| Map — auto-fit | Map — selection | Map — manual zoom |
| --- | --- | --- |
| ![map auto-fit](docs/media/map.png) | ![map selection](docs/media/map-select.png) | ![map zoom](docs/media/map-zoom.png) |

| Chat — channel | Chat — switch (key 5) | Chat — canned (key 6) |
| --- | --- | --- |
| ![chat channel](docs/media/chat-channel.png) | ![chat switch](docs/media/chat-switch.png) | ![chat canned](docs/media/chat-canned.png) |

| Node detail (key 8 from Nodes) |
| --- |
| ![node detail](docs/media/node-detail.png) |

| Tools — list | Tools — Mesh stats | Tools — Packet log |
| --- | --- | --- |
| ![tools list](docs/media/tools-list.png) | ![tools mesh stats](docs/media/tools-mesh.png) | ![tools packet log](docs/media/tools-pktlog.png) |

| Settings — list | Settings — edit (Long name) | Settings — typing |
| --- | --- | --- |
| ![settings list](docs/media/settings-list.png) | ![settings edit](docs/media/settings-edit.png) | ![settings typing](docs/media/settings-typing.png) |

Captured from the desktop SDL simulator at native 320×170, fed by the scripted Client-API peer (6
positioned nodes around Rimini, two channels). Each peer's radar dot shares its colour with the
side-column name; key `7` selects a node, keys `5`/`6` zoom. In CHATS, key `5` switches channel
(`#LongFast` → `#Private`) and key `6` opens the canned-reply picker.

## Architecture

```
meshtasticd :4403  ──Client API (framed protobuf)──▶  MeshtasticClientSource (reader thread)
                                                          │  on_node → EntityStore.upsert
                                                          │  on_message → MessageLog.add
                                                          │  on_channel → ChannelTable.upsert
                                                          ▼
                                       MeshtasticScreen (UI timer snapshots all) ─▶ Nodes / Chats / Map
```

Protobufs are pre-generated nanopb under `proto/` (vendored; see `proto/PIN.md`) — the build compiles
only C, no protoc/python.

## Build & run (desktop SDL simulator)

```bash
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg --target meshtastic_app
./build/linux-x86-64/apps/meshtastic/Debug/meshtastic_app   # SDL window, native 320×170
```

Needs a `meshtasticd` on `127.0.0.1:4403` to show data (override with `MESHTASTICD_HOST`/`MESHTASTICD_PORT`):

```bash
docker run -d --name mtd-sim -p 127.0.0.1:4403:4403 \
  meshtastic/meshtasticd:latest meshtasticd -s --fsdir=/var/lib/meshtasticd
```

Keys: `4` cycles the view (Chats→Nodes→Map→Tools→Settings), `5`–`8` are per-view actions, `ESC` quits.

> Note: a single `meshtasticd -s` node has no peer, so no incoming messages appear. To exercise receive,
> use a scripted peer or two nodes (Meshtasticator) — see the `meshtastic-dev-testbench` dev note.
