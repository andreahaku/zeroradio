#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# meshtastic_proto — vendored nanopb runtime + generated Meshtastic protobufs.
# Pre-generated C (see proto/PIN.md), so the build needs no protoc/python — it
# just compiles C, including under the cp0 aarch64 cross toolchain. Kept app-local
# (not in radio_toolkit) so adsb/sdr stay free of protobuf.
# ====================================================
file(GLOB MESHTASTIC_PROTO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/nanopb/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/meshtastic/*.pb.c"
)

add_library(meshtastic_proto STATIC ${MESHTASTIC_PROTO_SOURCES})
target_include_directories(meshtastic_proto
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/proto          # "meshtastic/mesh.pb.h"
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/nanopb   # <pb.h>, "pb_decode.h"
)
# Vendored/generated C: don't gate it on the project's warning flags.
target_compile_options(meshtastic_proto PRIVATE -w)

# ====================================================
# meshtastic_app — the Meshtastic mesh client executable
# ====================================================
file(GLOB_RECURSE MESHTASTIC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_executable(meshtastic_app ${MESHTASTIC_SOURCES})

target_include_directories(meshtastic_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)

target_link_libraries(meshtastic_app
    PRIVATE
        radio_toolkit
        lvgl
        meshtastic_proto
)
