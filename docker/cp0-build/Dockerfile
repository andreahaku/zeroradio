#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# Cross build environment for the CardputerZero (aarch64, Debian 13 trixie).
#
# The device runs Debian trixie, so building against trixie's own arm64 libraries
# with trixie's cross gcc gives binaries whose glibc/libstdc++ requirements match
# the device exactly, whatever toolchain the host happens to have. Used by
# scripts/cp0-docker-build.sh together with the `cp0-trixie` CMake preset.
FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture arm64 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
        crossbuild-essential-arm64 \
        cmake ninja-build git ca-certificates pkg-config file dpkg-dev python3 \
        nlohmann-json3-dev \
        libfreetype-dev:arm64 \
        libpng-dev:arm64 \
        libjpeg-dev:arm64 \
        zlib1g-dev:arm64 \
        libdrm-dev:arm64 \
        libfftw3-dev:arm64 \
        libasound2-dev:arm64 \
        libfmt-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

# Decoders bundled in the package because Debian's builds don't fit (see
# cmake/decoders.cmake): readsb needs RTL-SDR support, which Debian's lacks.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        librtlsdr-dev:arm64 \
        libzstd-dev:arm64 \
        libncurses-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*
