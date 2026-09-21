# device/meshtasticd — native meshtasticd for the Cap LoRa-1262

Configuration mirror for running a **native `meshtasticd`** on the [CardputerZero](https://shop.m5stack.com/pages/m5-cardputerzero) with the
M5Stack **Cap LoRa-1262** (SX1262 + ATGM336H GNSS) on the HAT port. The full hardware map,
install steps and acceptance procedure live in [`docs/cap-lora-1262.md`](../../docs/cap-lora-1262.md).

| File | Installs to | Purpose |
| --- | --- | --- |
| `config.yaml` | `/etc/meshtasticd/config.yaml` | SX1262 on `spidev0.1` (DIO1=GPIO23, BUSY=GPIO22, TCXO on DIO3), GNSS on `/dev/ttyS0`. |
| `cap-lora-init.sh` | `/usr/local/sbin/cap-lora-init` | Pre-start init: drives the PI4IOE5V6408 expander (antenna RF switch P0) and wakes the GNSS. |
| `meshtasticd-cap-lora.conf` | `/etc/systemd/system/meshtasticd.service.d/` | Systemd drop-in running `cap-lora-init` as `ExecStartPre` on every daemon start. |

Acceptance: [`scripts/cap-lora-accept.sh`](../../scripts/cap-lora-accept.sh) (15 asserts).
