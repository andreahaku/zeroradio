#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
# SPDX-License-Identifier: MIT
#
# Acceptance test for the Cap LoRa-1262 (SX1262 + ATGM336H GNSS) driven by a
# native meshtasticd on the CardputerZero. Runs ON the device:
#
#   ssh root@<device> 'bash -s' < scripts/cap-lora-accept.sh
#
# PASS requires ALL of:
#   1. meshtasticd systemd service active, with non-empty logs for the CURRENT
#      service invocation (stale evidence from earlier runs never counts)
#   2. radio genuinely initialised: the firmware's own "SX126x init result 0"
#      line in the current invocation, no radio-failure lines, and NO simradio
#      fallback (portduino silently falls back to a simulated radio)
#   3. the rendered meshtasticd config declares the real hardware contract:
#      sx1262 module on spidev0.1, GNSS on /dev/ttyS0 (baud is autoprobed by
#      the firmware; the ATGM336H detection line is asserted instead)
#   4. the cap's PI4IOE5V6408 IO-expander (i2c-1 0x43) has P0 driven high
#      (antenna RF switch enabled): direction=output, output=1, High-Z cleared
#   5. Client API answers on 127.0.0.1:4403 with region EU_868
#   6. GNSS chain alive in the current invocation (GPS probe/detect evidence)
#
# This file is the frozen reward for the cap-lora integration task: it is not
# edited to make the integration pass (reward-guard.sh ceremony required).
set -u

FAILS=0
say() { printf '%s\n' "$*"; }
check() { # $1 = 0/1 ok, $2 = name
    if [ "$1" -eq 0 ]; then say "PASS: $2"; else say "FAIL: $2"; FAILS=$((FAILS + 1)); fi
}

# --- 1. service active + logs scoped to the CURRENT invocation ---
systemctl is-active --quiet meshtasticd
check $? "meshtasticd service active"

INVOCATION="$(systemctl show -p InvocationID --value meshtasticd 2>/dev/null)"
if [ -n "$INVOCATION" ]; then
    LOG="$(journalctl --no-pager "_SYSTEMD_INVOCATION_ID=$INVOCATION" 2>/dev/null)"
else
    LOG=""
fi
[ -n "$LOG" ]
check $? "non-empty logs for the current service invocation"

# --- 2. real SX126x init, no failures, no simulated radio ---
# Firmware wording (SX126xInterface.cpp): "SX126x init result 0" on success.
printf '%s' "$LOG" | grep -qiE 'sx126[x2].*init result 0'
check $? "SX126x init result 0 (current invocation)"
printf '%s' "$LOG" | grep -qiE 'failed to find (any )?radio|radio init failed|init result [^0]' && RADIO_FAIL=0 || RADIO_FAIL=1
check $((1 - RADIO_FAIL)) "no radio-failure lines (current invocation)"
# Active config = config.yaml + config.d drop-ins, with YAML comments stripped
# so a commented example can never satisfy (or trip) a check. available.d
# templates are NOT active config and are deliberately excluded.
CFG="$(cat /etc/meshtasticd/config.yaml /etc/meshtasticd/config.d/* 2>/dev/null | sed 's/#.*$//')"

{ printf '%s' "$LOG" | grep -qi 'simradio'; } || { printf '%s' "$CFG" | grep -qi 'simradio'; } && SIM=0 || SIM=1
check $((1 - SIM)) "no simradio fallback (log + active config)"

# --- 3. rendered config declares the real hardware contract ---
printf '%s' "$CFG" | grep -qiE 'module:[[:space:]]*sx1262'
check $? "config: Module sx1262"
printf '%s' "$CFG" | grep -q 'spidev0\.1'
check $? "config: spidev0.1"
printf '%s' "$CFG" | grep -q '/dev/ttyS0'
check $? "config: GNSS on /dev/ttyS0"
# The cap's GNSS module is an ATGM336H; portduino autoprobes the baudrate (no
# Baud config key exists), so require the firmware's own detection line instead.
printf '%s' "$LOG" | grep -q 'ATGM336H detected'
check $? "GNSS module ATGM336H detected by firmware (current invocation)"

# --- 4. antenna RF switch: PI4IOE5V6408 P0 high (bit0) ---
DIR="$(i2cget -y 1 0x43 0x03 2>/dev/null)"; OUT="$(i2cget -y 1 0x43 0x05 2>/dev/null)"; HIZ="$(i2cget -y 1 0x43 0x07 2>/dev/null)"
[ -n "$DIR" ] && [ $(( DIR & 1 )) -eq 1 ]
check $? "expander P0 direction = output"
[ -n "$OUT" ] && [ $(( OUT & 1 )) -eq 1 ]
check $? "expander P0 output = high"
[ -n "$HIZ" ] && [ $(( HIZ & 1 )) -eq 0 ]
check $? "expander P0 High-Z cleared"

# --- 5. Client API + region ---
if ! command -v meshtastic >/dev/null 2>&1; then
    say "FAIL: meshtastic python CLI not installed (environment prerequisite)"
    FAILS=$((FAILS + 1))
    INFO=""
else
    INFO="$(timeout 30 meshtastic --host 127.0.0.1 --info 2>/dev/null)" || INFO=""
fi
[ -n "$INFO" ]
check $? "Client API answers on 127.0.0.1:4403"
printf '%s' "$INFO" | grep -q 'EU_868'
check $? "LoRa region is EU_868"

# --- 6. GNSS chain alive in the current invocation ---
printf '%s' "$LOG" | grep -qiE 'gps.*(detect|probe|found|init|serial)|serial.*gps'
check $? "GNSS probe/detect evidence (current invocation)"

say "---"
if [ "$FAILS" -eq 0 ]; then say "CAP-LORA ACCEPTANCE: PASS"; exit 0; fi
say "CAP-LORA ACCEPTANCE: FAIL ($FAILS check(s) failed)"
exit 1
