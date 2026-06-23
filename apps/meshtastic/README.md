# meshtastic — Meshtastic mesh client (scaffold)

A Meshtastic LoRa-mesh client for the CardputerZero, built on the shared `radio_toolkit`. It is a
**client of a local `meshtasticd` daemon** (Client API over `127.0.0.1:4403`), not a reimplementation of
the radio — see the design docs in [`../../../radio-apps/`](../../../radio-apps/) (`09`, `09a`, `09b`,
`09c`).

## Status: scaffold

App shell only — it builds, runs, and shows the five views (**Chats / Nodes / Map / Tools / Settings**)
cycled by the **5-key NavBar** (keys `4`–`8`, `4` cycles the page; `ESC` quits), exactly like the ADS-B
and SDR apps. No Meshtastic data yet: the views are placeholders. Next is the `MeshtasticClientSource`
(toolkit) per `radio-apps/09b`.

## Build & run (desktop SDL simulator)

```bash
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64-dbg --target meshtastic_app
./build/linux-x86-64/apps/meshtastic/Debug/meshtastic_app   # SDL window, native 320×170
```

Keys: `4` cycles the view (Chats→Nodes→Map→Tools→Settings), `5`–`8` are the per-view actions (wired as the
views are implemented), `ESC` quits.
