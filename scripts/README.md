# scripts/

Helper scripts for building ZeroRadio and for checking a CardputerZero. None of them is part of the app at runtime. Read the header comment of a script before you run it.

| Script | What it does | Safety |
| --- | --- | --- |
| `cp0-docker-build.sh` | Cross-builds the apps for the CardputerZero in a Debian trixie container (`docker/cp0-build`, preset `cp0-trixie`). `--package` also builds the `zeroradio` Debian package. | Builds only. Needs Docker. |
| `sdr-profile.sh` | Profiles `sdr_app` on the device: CPU per core, memory, temperature, throttling. See [`docs/sdr-device-profile.md`](../docs/sdr-device-profile.md). | Read-only. |
| `cap-lora-accept.sh` | Acceptance test for the Cap LoRa-1262 with a native `meshtasticd` (SPI, GPIO, RF switch, GNSS, region). See [`docs/cap-lora-1262.md`](../docs/cap-lora-1262.md). | Read-only checks on the device. |
