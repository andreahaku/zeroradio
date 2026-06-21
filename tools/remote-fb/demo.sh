#!/usr/bin/env bash
#
# SPDX-License-Identifier: MIT
#
# One-command Path B demo: run a cardputer-radio app HEADLESS (as the Pi would)
# and open the remote-fb viewer so you can SEE and DRIVE the 320x170 "virtual
# screen" on this machine. The viewer window runs in YOUR terminal session, so
# it shows up on your display and your keyboard (4-8, Esc, digits) drives the app.
#
#   ./tools/remote-fb/demo.sh            # ADS-B app (mock data, no dongle needed)
#   ./tools/remote-fb/demo.sh sdr        # SDR app (set SDR_RTLTCP for a real dongle)
#
# Close the viewer window (or Ctrl+C) to stop both.
#
set -euo pipefail

APP="${1:-adsb}"
PORT="${REMOTE_FB_PORT:-5800}"
SCALE="${REMOTE_FB_SCALE:-1}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP_BIN="$ROOT/build/linux-x86-64/apps/$APP/Debug/${APP}_app"
VIEWER="$ROOT/tools/remote-fb/build/remote-fb-viewer"

[[ -x "$APP_BIN" ]] || { echo "missing $APP_BIN — build first: cmake --build --preset linux-x86-64-dbg"; exit 1; }
[[ -x "$VIEWER" ]]  || { echo "missing $VIEWER — build first: cmake -S tools/remote-fb -B tools/remote-fb/build && cmake --build tools/remote-fb/build"; exit 1; }

cd "$ROOT"
pkill -f "${APP}_app" 2>/dev/null || true
sleep 0.2

echo ">> ${APP}_app headless on REMOTE_FB=$PORT (log: /tmp/cr-${APP}.out)"
REMOTE_FB="$PORT" "$APP_BIN" >"/tmp/cr-${APP}.out" 2>&1 &
APP_PID=$!
trap 'kill "$APP_PID" 2>/dev/null || true' EXIT
sleep 1

echo ">> opening viewer (close the window to stop)"
"$VIEWER" 127.0.0.1 "$PORT" "$SCALE"
