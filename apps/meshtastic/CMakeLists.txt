#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
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
# meshtastic_decoder — pure FromRadio decoder (framing + protobuf union dispatch ->
# typed events), no sockets/threads/LVGL, so the unit test links it standalone.
# Extracted from src/net/meshtastic_client_source. See src/decode/meshtastic_decoder.h.
# ====================================================
add_library(meshtastic_decoder STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decode/meshtastic_decoder.cpp
)
target_include_directories(meshtastic_decoder
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/decode
)
target_link_libraries(meshtastic_decoder PUBLIC meshtastic_proto)

# ----------------------------------------------------------------------
# meshtastic_decoder_test — parity test against frozen, two-source FromRadio vectors.
# Host-only (desktop build); registered with CTest.
# ----------------------------------------------------------------------
if(USE_DESKTOP)
    add_executable(meshtastic_decoder_test
        ${CMAKE_CURRENT_SOURCE_DIR}/test/meshtastic_decoder_test.cpp)
    target_link_libraries(meshtastic_decoder_test PRIVATE meshtastic_decoder)
    add_test(NAME meshtastic_decoder_test COMMAND meshtastic_decoder_test)
endif()

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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/decode
)

find_package(Threads REQUIRED)

target_link_libraries(meshtastic_app
    PRIVATE
        radio_toolkit
        lvgl
        meshtastic_proto
        Threads::Threads
)
