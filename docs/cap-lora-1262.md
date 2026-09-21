# Cap LoRa-1262 on the CardputerZero — native meshtasticd integration

The [M5Stack Cap LoRa-1262](https://docs.m5stack.com/en/cap/Cap_LoRa-1262) (SX1262 LoRa
transceiver + ATGM336H GNSS) attaches to the [CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero)'s HAT port. Rather than driving the
radio from the app (RadioLib/TinyGPSPlus are Arduino libraries for the ESP32-based
Cardputer-Adv), the CardputerZero runs a **native `meshtasticd`** (Linux portduino build) that
owns the radio and GPS; our `meshtastic_app` stays what it already is — a Client-API client on
`127.0.0.1:4403`. Verified end-to-end on 2026-07-07: `SX126x init result 0`, `ATGM336H
detected`, region `EU_868`, real GNSS fix shown by the app on the device.

## Hardware map (probed on the real device, schematic v0.3)

The CardputerZero hardware is described in the [M5Stack documentation](https://docs.m5stack.com/en/CardputerZero). Its HAT port (JP5) exposes SPI0 (kernel CS1), I2C1, the mini-UART, GPIO22/23 and
a gated 5 V rail (`G5_HAT_5VOUT_EN`, high by default). The cap's signals land as:

| Cap signal | CardputerZero | Notes |
| --- | --- | --- |
| SX1262 SPI | `/dev/spidev0.1` (SPI0, CS1 = GPIO7, kernel-managed) | `GetStatus` + reg `0x0740` = `0x14 0x24` proves the chip |
| SX1262 DIO1 (IRQ) | **GPIO23** | identified empirically: RX-timeout IRQ raises it |
| SX1262 BUSY | **GPIO22** | by exclusion (only two free HAT GPIOs) |
| SX1262 RESET | not wired to a CM0 GPIO | power-on reset on the cap; omit `Reset` in config |
| TCXO | DIO3, 1.8 V | without it `SetRx` fails with `XOSC_START_ERR` (0x0020) |
| RF antenna switch | PI4IOE5V6408 IO expander, i2c-1 addr `0x43`, **P0 high** | done by `cap-lora-init` (ExecStartPre) before meshtasticd |
| GNSS | `/dev/ttyS0` (mini-UART, 115200 8N1) | the module is a **GP-02** (CASIC — meshtasticd detects it as "ATGM336H"), with a VBAT backup cell; autoprobed baud, needs `position.gps_mode ENABLED` |

Per the cap schematic (`U214-sche-Cap-LoRa1262_SCH_V1.1`): the radio is a *Stamp LoRa-1262
Mini* module, the expander's **P1 = SHUT_DOWN** of the MAX2659 GPS antenna LNA (leave it
alone), and the cap's 5 V comes from the HAT's **unswitched** supply pin — toggling
`G5_HAT_5VOUT_EN` (GPIO5) does NOT power-cycle the cap.

⚠️ **Never power the cap without the RP-SMA antenna installed** — TX into an open PA can
permanently damage the radio (M5 warning).

## Install (on the device)

```bash
# 1. meshtasticd from the official OBS repo (Debian 13 / trixie, arm64) plus
#    the tools the RF-switch unit and CLI setup need. The key goes in a
#    dedicated keyring (not the global trusted.gpg.d) and is installed
#    atomically so a failed download can't leave a truncated key.
curl -fsSL https://download.opensuse.org/repositories/network:/Meshtastic:/beta/Debian_13/Release.key \
  | gpg --dearmor > /tmp/meshtasticd.gpg
install -o root -g root -m 644 /tmp/meshtasticd.gpg /etc/apt/keyrings/meshtasticd.gpg && rm /tmp/meshtasticd.gpg
echo 'deb [signed-by=/etc/apt/keyrings/meshtasticd.gpg] https://download.opensuse.org/repositories/network:/Meshtastic:/beta/Debian_13/ /' \
  > /etc/apt/sources.list.d/meshtasticd.list
apt update && apt install -y meshtasticd i2c-tools python3-venv

# 2. hardware config + cap init hook (from this repo's device/meshtasticd/)
cp device/meshtasticd/config.yaml          /etc/meshtasticd/config.yaml
cp device/meshtasticd/cap-lora-init.sh     /usr/local/sbin/cap-lora-init
chmod 755 /usr/local/sbin/cap-lora-init
mkdir -p /etc/systemd/system/meshtasticd.service.d
cp device/meshtasticd/meshtasticd-cap-lora.conf /etc/systemd/system/meshtasticd.service.d/cap-lora.conf
systemctl daemon-reload
systemctl enable --now meshtasticd.service

# 3. meshtastic python CLI (used by the acceptance script and for node config)
python3 -m venv /opt/meshtastic-cli
/opt/meshtastic-cli/bin/pip install meshtastic
ln -sf /opt/meshtastic-cli/bin/meshtastic /usr/local/bin/meshtastic

# 4. node configuration (persisted in the node's own prefs). Wait for the
#    Client API first — the daemon takes a few seconds to start listening.
until timeout 2 bash -c '</dev/tcp/127.0.0.1/4403' 2>/dev/null; do sleep 1; done
meshtastic --host 127.0.0.1 --set lora.region EU_868
meshtastic --host 127.0.0.1 --set position.gps_mode ENABLED
```

## Acceptance

`scripts/cap-lora-accept.sh` is the frozen end-to-end check (15 asserts): service active,
`SX126x init result 0` in the *current* systemd invocation, no simradio fallback, the hardware
contract in the rendered config, expander P0 read back high over i2c, Client API answering with
region `EU_868`, and the firmware's own `ATGM336H detected` line.

```bash
ssh root@<device> 'bash -s' < scripts/cap-lora-accept.sh
```

## Gotchas learned on the way

- **portduino falls back to a simulated radio** when the LoRa section is missing/broken — a
  green service or answering API is NOT proof the radio works; only the `SX126x init result 0`
  line (scoped to the current invocation) is.
- The **TCXO is mandatory**: without `DIO3_TCXO_VOLTAGE: true` the chip reports
  `XOSC_START_ERR` and RX/TX never start, while SPI register reads still work.
- **No `Baud` key exists** for GPS in portduino config — the firmware autoprobes; assert the
  `ATGM336H detected` log line instead of config prose.
- `position.gps_mode` defaults to `NOT_PRESENT` (2) on portduino: without setting it to
  `ENABLED` meshtasticd never opens the serial port (no GPS log lines at all).
- **CASIC hardsleep survives restarts (and power cycles — VBAT cell)**: meshtasticd
  duty-cycles the GPS with `$PCAS12` when there's no fix; if the daemon restarts during a
  sleep window the baud autoprobe finds a silent module and **gives up for the whole run**.
  That's why `cap-lora-init` runs as `ExecStartPre=+` on *every* start (a `Requires=` oneshot
  would not re-run on restart) and wakes the module with a benign `$PCAS06,0*1B` query at
  115200. Verified: restart mid-hardsleep → `ATGM336H detected` at the first probe attempt.
- `gpioset` (libgpiod v2) **holds the line until killed** and releasing can leave the last
  written register value — use `pinctrl set 5 op dh` for persistent GPIO writes when probing.
- Omit `CS:` from the Lora config — GPIO7 is claimed by the kernel SPI driver (`spi0 CS1`),
  and spidev toggles it natively.
- Status-echo bytes over the HAT's SPI isolation buffer read as `0xAA` garbage; the *data*
  phase is clean. Don't diagnose the chip from status bytes alone.

## Not covered (needs a second LoRa node / open sky)

- Over-the-air TX/RX with a peer Meshtastic node (none available at integration time).
- Long-run GNSS behaviour (the indoor fix above was acquired near a window; the antenna is the
  cap's built-in ceramic patch).
