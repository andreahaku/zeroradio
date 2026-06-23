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
  recoloured: self green, peers info-blue).
- **Chat send + compose** — the `8` (write) key enters a compose row (raw key capture via
  `platform::set_key_capture`); type, **`Enter` sends and returns**, `Esc` cancels. `send_text` queues a
  `ToRadio` `MeshPacket` (the reader loop writes it) and echoes the message to our own feed.
- **ACK color-outline** — `ROUTING_APP` matched by `request_id` updates a sent message's state; the feed
  shows a delivery dot: amber (pending) → green (delivered) / red (failed).

Placeholders / pending: channel switch / canned / reactions / DMs, Map (PPI), Tools, Settings. See
`radio-apps/09b` (build order) and `09c` (per-screen design).

## Architecture

```
meshtasticd :4403  ──Client API (framed protobuf)──▶  MeshtasticClientSource (reader thread)
                                                          │  on_node → EntityStore.upsert
                                                          │  on_message → MessageLog.add
                                                          ▼
                                       MeshtasticScreen (UI timer snapshots both) ─▶ Nodes / Chats views
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
