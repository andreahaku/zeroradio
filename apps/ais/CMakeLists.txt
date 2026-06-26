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
endif()

# ====================================================
# ais_app — the AIS vessel viewer executable (port of apps/adsb). The on-device
# decode (ais_decoder) turns host-produced !AIVDM sentences into Vessels merged
# into the shared EntityStore; the screen lists them. See apps/ais/README.md.
# ====================================================
add_executable(ais_app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/vessel_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/ais_viewmodel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view/ais_screen.cpp
)

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
