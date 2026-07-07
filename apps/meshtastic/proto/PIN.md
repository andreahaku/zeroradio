# Vendored protobuf stack (nanopb)

Pre-generated nanopb code so the build compiles **only C** — no build-time `protoc`/python
dependency (important for the `cp0-aarch64` cross build and reproducibility).

## Pins

- **meshtastic/protobufs**: `da60cee584c6dc1efbb4a3809b98666505179b85`
- **nanopb runtime**: `0.4.9.1` (`nanopb/` — `pb.h`, `pb_common`, `pb_decode`, `pb_encode`)
- generator: `nanopb==0.4.9.1` (pip), `protoc` 3.21.x

## Layout

- `nanopb/` — nanopb runtime (vendored from the nanopb 0.4.9.1 release).
- `meshtastic/` — generated `*.pb.{c,h}` for the protos this client needs and their import
  closure (atak, channel, config, device_ui, mesh, module_config, portnums, telemetry, xmodem).

## Regenerate (when bumping the protobufs pin)

Run in a clean container so host `protoc`/python versions don't matter:

```bash
git clone --depth 1 https://github.com/meshtastic/protobufs.git
docker run --rm -v "$PWD":/work python:3.12-slim bash -c '
  apt-get update -qq && apt-get install -y -qq --no-install-recommends protobuf-compiler
  pip install -q nanopb==0.4.9.1
  cd /work && mkdir -p out
  nanopb_generator -I protobufs -D out $(ls protobufs/meshtastic/*.proto)'
# then copy out/meshtastic/*.pb.{c,h} here and update the pin SHA above.
```
