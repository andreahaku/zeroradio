#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# ais_decoder — pure AIVDM position-report decoder (no LVGL/toolkit deps), so the
# unit test links it standalone. See apps/ais/README.md.
# ====================================================
add_library(ais_decoder STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder/ais_decoder.cpp
)
target_include_directories(ais_decoder
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder
)

# ----------------------------------------------------------------------
# ais_decoder_test — parity test against frozen, two-source AIVDM vectors.
# Host-only (desktop build); registered with CTest.
# ----------------------------------------------------------------------
if(USE_DESKTOP)
    add_executable(ais_decoder_test ${CMAKE_CURRENT_SOURCE_DIR}/test/ais_decoder_test.cpp)
    target_link_libraries(ais_decoder_test PRIVATE ais_decoder)
    add_test(NAME ais_decoder_test COMMAND ais_decoder_test)

    # Loopback test for the live UDP NMEA source (lives in radio_toolkit).
    add_executable(nmea_net_test ${CMAKE_CURRENT_SOURCE_DIR}/test/nmea_net_test.cpp)
    target_link_libraries(nmea_net_test PRIVATE ais_decoder radio_toolkit lvgl)
    add_test(NAME nmea_net_test COMMAND nmea_net_test)
endif()

# ====================================================
# ais_app — the AIS vessel viewer executable (port of apps/adsb). The on-device
# decode (ais_decoder) turns host-produced !AIVDM sentences into Vessels merged
# into the shared EntityStore; the screen lists them. See apps/ais/README.md.
# ====================================================
# Same GLOB pattern as the sibling apps; the decoder sources are excluded —
# they live in the ais_decoder static lib (linked below).
file(GLOB_RECURSE AIS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER AIS_SOURCES EXCLUDE REGEX "/decoder/ais_decoder\\.cpp$")

add_executable(ais_app ${AIS_SOURCES})

target_include_directories(ais_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)

target_compile_definitions(ais_app
    PRIVATE
        APP_MOCK_NMEA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/mock/ais.nmea"
)

target_link_libraries(ais_app
    PRIVATE
        radio_toolkit
        lvgl
        ais_decoder
)
