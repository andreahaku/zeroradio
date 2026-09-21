# Publishing ZeroRadio

How to build the `zeroradio` package, check it against the AppStore rules and submit it. The AppStore reads the package plus `app-builder.json` at the repository root.

## 1. Set the version

The version lives in one place: `project(zeroradio VERSION x.y.z)` in `CMakeLists.txt`. It feeds the package, the hub title ("ZeroRadio x.y.z") and the About page. Copy the same value into `app-builder.json` (`version`) and add a section to `CHANGELOG.md`.

## 2. Build the package

The package builds in a Debian trixie container, so its binaries need the same glibc and libstdc++ as the CardputerZero. It needs Docker.

```bash
scripts/cp0-docker-build.sh --package
# -> build/cp0-trixie/zeroradio_<version>_arm64.deb
```

The build also compiles the bundled decoders from pinned tags (`cmake/decoders.cmake`): readsb for ADS-B and AIS-catcher for AIS.

## 3. Check the package

Check the install paths with the store's own policy script (from the `CardputerZero/packages` repository):

```bash
python3 packages/.github/scripts/install_path_policy.py zeroradio \
    build/cp0-trixie/zeroradio_<version>_arm64.deb /tmp/rejected.txt
# /tmp/rejected.txt stays empty when every path is allowed
```

Check the manifest with the validator of the CardputerZero Template:

```bash
cmake -DPACKAGE_FILE=build/cp0-trixie/zeroradio_<version>_arm64.deb \
    -P <Template>/.agents/skills/cardputerzero-package-release/scripts/validate_app_builder.cmake
```

The Template validator also expects a Debian revision in the file name (`name_version_revision_arm64.deb`). The store CI instead requires `name_version_arch.deb`, the Debian default that CPack produces. Keep the CPack name, and ignore that single validator message.

The store rules the manifest must meet: a summary of 80 characters or less, 1 or 2 categories from the fixed list, 1 to 6 screenshots of 320x170 PNG, a square icon of 128 to 512 px, all 7 permission flags, a unique 4-character share code, and no template placeholder text.

## 4. Test on a device

Install the package the way the AppStore does, with apt resolving the dependencies:

```bash
sudo apt install ./zeroradio_<version>_arm64.deb
```

Then open ZeroRadio from the launcher and check every app with a dongle attached: SDR audio, a Scanner sweep and Open in SDR, ISM devices, ADS-B aircraft, AIS ships, Settings > Location, H (help), TAB (SDR <-> Scanner), Esc and a 3 s Esc hold.

## 5. Screenshots and icon

- Screenshots: `docs/media/*.png`, 320x170, taken from the device framebuffer (`/dev/fb0`, RGB565) and listed in `app-builder.json`.
- Icon: `assets/images/zeroradio.png` (256x256), rendered from `assets/images/zeroradio.svg` with `rsvg-convert -w 256 -h 256`.

## 6. Submit

Submit with the `czdev` tool from `CardputerZero/AppBuilder` (`czdev publish --deb <file>`) or through the developer portal at dev.cardputer.cc. The first release of an app goes to manual review, and the reviewers ask for a short video of the app on a real device.

For the review, state in the submission:

- External hardware: an RTL-SDR dongle in the USB-A port, and optionally a GPS (Cap LoRa-1262-GPS or USB).
- Network: none. The apps talk to their decoders over the loopback interface only, and AIS-catcher runs with community sharing off (`-X off`).
- Licences: the source is MIT. The package also contains readsb and AIS-catcher (GPL-3.0) as separate programs, and the SDR app links FFTW (GPL-2.0-or-later). See `CREDITS.md`.

## Store identity

| Field | Value |
| --- | --- |
| Title (`app_name`) | ZeroRadio |
| Package (`package_name`) | zeroradio |
| Share code | ZRAD |
| Category | Radio & Comms |

The package name cannot change after the first release.
