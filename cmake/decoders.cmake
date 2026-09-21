#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# Decoders bundled in the package (device cross build only).
#
# The apps drive the dongle through helper tools. Tools whose Debian trixie
# build works are package Depends (rtl-sdr, rtl-433); the ones below are not
# usable from Debian, so they are built from pinned upstream sources and
# installed next to the app binaries, where toolkit::find_tool() finds them.
#
#   readsb      (GPL-3.0)  ADS-B. Debian's readsb is built without RTL-SDR
#                          support, and its package enables a boot-time service
#                          that would grab the dongle.
#   AIS-catcher (GPL-3.0)  AIS. Not packaged by Debian. Built RTL-SDR-only (every
#                          other SDR, DB and web option off) to keep it small.
# ====================================================
include(ExternalProject)

set(RADIO_READSB_TAG "v3.16.16")
set(_readsb_pc "${CMAKE_SYSROOT}/usr/lib/${CM0_MULTIARCH}/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")

ExternalProject_Add(readsb
    GIT_REPOSITORY    https://github.com/wiedehopf/readsb.git
    GIT_TAG           ${RADIO_READSB_TAG}
    GIT_SHALLOW       TRUE
    BUILD_IN_SOURCE   TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ${CMAKE_COMMAND} -E env PKG_CONFIG_LIBDIR=${_readsb_pc}
                      make -j4 CC=${CMAKE_C_COMPILER} RTLSDR=yes readsb
    INSTALL_COMMAND   ""
)
ExternalProject_Get_Property(readsb SOURCE_DIR)
set(RADIO_READSB_BIN "${SOURCE_DIR}/readsb")

set(RADIO_AISCATCHER_TAG "v0.70")

ExternalProject_Add(aiscatcher
    GIT_REPOSITORY    https://github.com/jvde-github/AIS-catcher.git
    CMAKE_GENERATOR   Ninja
    GIT_TAG           ${RADIO_AISCATCHER_TAG}
    GIT_SHALLOW       TRUE
    CMAKE_ARGS        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
                      -DCM0_SDK_ROOT=${CM0_SDK_ROOT}
                      -DCMAKE_SYSROOT=${CMAKE_SYSROOT}
                      -DCMAKE_BUILD_TYPE=Release
                      -DRTLSDR=ON
                      -DAIRSPY=OFF -DAIRSPYHF=OFF -DSDRPLAY=OFF -DHACKRF=OFF -DHYDRASDR=OFF
                      -DSOAPYSDR=OFF -DSOXR=OFF -DZLIB=OFF -DSAMPLERATE=OFF -DZMQ=OFF
                      -DPSQL=OFF -DSQLITE=OFF -DOPENSSL=OFF -DNMEA2000=OFF -DWEBVIEWER=OFF
    INSTALL_COMMAND   ""
)
ExternalProject_Get_Property(aiscatcher BINARY_DIR)
set(RADIO_AISCATCHER_BIN "${BINARY_DIR}/AIS-catcher")
