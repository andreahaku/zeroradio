#!/bin/sh
# sdr-profile.sh — on-device profiler for sdr_app on the CardputerZero (CM0).
# Run ON the device (root@pi) WHILE sdr_app is streaming at the target sample rate.
# Captures per-core CPU, per-process CPU/RSS, RAM, SoC temperature and throttle flags.
#
# Usage:   ./sdr-profile.sh [seconds] [proc-name]
#   seconds   sampling window (default 30)
#   proc-name process to track (default sdr_app)
#
# Output is plain text to stdout; redirect to a file to keep the run.

set -eu

DUR="${1:-30}"
PROC="${2:-sdr_app}"
INT=2  # sample interval (s)

echo "===== sdr-profile $(date -u +%Y-%m-%dT%H:%M:%SZ) ====="
echo "window=${DUR}s interval=${INT}s proc=${PROC}"
echo

echo "----- static -----"
echo "kernel : $(uname -srm)"
echo "model  : $(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo '?')"
echo "nproc  : $(nproc)"
grep -E 'MemTotal|MemAvailable' /proc/meminfo 2>/dev/null || true
echo

PID="$(pgrep -x "$PROC" 2>/dev/null | head -1 || true)"
if [ -z "$PID" ]; then
  PID="$(pgrep -f "$PROC" 2>/dev/null | head -1 || true)"
fi
if [ -z "$PID" ]; then
  echo "WARN: process '$PROC' not found — start it first, then re-run." >&2
else
  echo "tracking PID=$PID ($(cat /proc/$PID/comm 2>/dev/null || echo ?))"
fi
echo

have() { command -v "$1" >/dev/null 2>&1; }

echo "----- sampling (${DUR}s) -----"
END=$(( $(cut -d. -f1 /proc/uptime) + DUR ))
while [ "$(cut -d. -f1 /proc/uptime)" -lt "$END" ]; do
  TS=$(date -u +%H:%M:%S)
  TEMP="$(vcgencmd measure_temp 2>/dev/null | sed 's/temp=//' || echo '?')"
  THR="$(vcgencmd get_throttled 2>/dev/null || echo '?')"
  CLK="$(vcgencmd measure_clock arm 2>/dev/null | sed 's/.*=//' || echo '?')"
  MEMAVAIL="$(awk '/MemAvailable/{print int($2/1024)"M"}' /proc/meminfo)"
  if [ -n "${PID:-}" ] && [ -d "/proc/$PID" ]; then
    # %CPU and RSS for the tracked process via top (one batch iter)
    PLINE="$(top -b -n1 -p "$PID" 2>/dev/null | awk -v p="$PID" '$1==p{printf "cpu=%s%% rss=%s", $9, $6}')"
  else
    PLINE="proc gone"
  fi
  echo "$TS  temp=$TEMP  arm=$CLK  memavail=$MEMAVAIL  $THR  | $PROC: $PLINE"
  sleep "$INT"
done
echo

echo "----- per-core CPU busy%% (sampled ${INT}s, from /proc/stat) -----"
if have mpstat; then
  mpstat -P ALL "$INT" 1 | tail -n $(( $(nproc) + 2 ))
else
  # Portable per-core busy% via two /proc/stat snapshots INT apart.
  awk -v intv="$INT" '
    function snap(arr,   l,f,n) {
      while ((getline l < "/proc/stat") > 0) {
        n = split(l, f, /[ \t]+/)
        if (f[1] ~ /^cpu[0-9]+$/) {
          idle = f[5] + f[6]                       # idle + iowait
          tot  = 0; for (i=2;i<=n;i++) tot += f[i]
          arr[f[1]"_idle"] = idle; arr[f[1]"_tot"] = tot
        }
      }
      close("/proc/stat")
    }
    BEGIN {
      snap(a); system("sleep " intv); snap(b)
      for (k in a) if (k ~ /_tot$/) {
        c = substr(k, 1, length(k)-4)
        dt = b[c"_tot"] - a[c"_tot"]; di = b[c"_idle"] - a[c"_idle"]
        if (dt > 0) printf "%-6s busy=%5.1f%%\n", c, 100*(dt-di)/dt
      }
    }' | sort
fi
echo

echo "----- final memory -----"
free -m 2>/dev/null || true
echo
echo "----- throttle decode -----"
echo "get_throttled bits: 0=under-volt 1=arm-freq-cap 2=throttled 3=soft-temp-limit"
echo "                    16=under-volt-occurred 17=arm-cap-occurred 18=throttle-occurred 19=soft-temp-occurred"
echo "===== end ====="
