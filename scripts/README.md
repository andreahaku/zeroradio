# scripts/ — operator scripts

One-off operator tooling for provisioning, diagnosing and validating the physical CardputerZero.
These are **not** part of the build or runtime; several are destructive or hard-code the
maintainer's host paths and device — read the header comment of each script before running it.

| Script | What it does | Safety |
| --- | --- | --- |
| `cap-lora-accept.sh` | Acceptance test for the Cap LoRa-1262 + native `meshtasticd` install (15 asserts: SPI, GPIO, RF switch, GNSS, region). See [`docs/cap-lora-1262.md`](../docs/cap-lora-1262.md). | Read-only checks on the device. |
| `sdr-profile.sh` | On-device profiler for `sdr_app` (per-core CPU, RSS, temperature, throttle flags). See [`docs/sdr-device-profile.md`](../docs/sdr-device-profile.md). | Read-only. |
| `usb-host-diag.sh` | USB host-mode diagnostics, run over SSH against the device. See [`docs/m5stack-usb-host-followup.md`](../docs/m5stack-usb-host-followup.md). | Read-only. |
| `cz-preflash-backup.sh` | Back up SD-card work/config before reflashing (mounts read-only). | Non-destructive. |
| `cz-flash.sh` | Flash the CardputerZero image to the SD card via `dd`. | **DESTRUCTIVE**; refuses to run unless the target matches the expected removable card. Hard-codes the maintainer's image/backup paths — edit before use. |
| `cz-inject-settings.sh` | Inject SSH access + WiFi into a freshly-flashed image from the pre-flash backup. | Writes to the mounted card; maintainer paths hard-coded. |
| `cz-inspect-image.sh` | Read-only inspection of a freshly-flashed image (provisioning method discovery). | Read-only. |
| `cz-wifi-diag.sh` | Diagnose why WiFi finds no networks on a fresh image. | Read-only. |
| `cz-wifi-unblock.sh` | Clear the persisted rfkill soft-block on WLAN. | Writes rfkill state. |
