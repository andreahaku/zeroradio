#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# radio_app — the "Radio" hub (Option A1)
# A thin launcher that renders a menu and posix_spawn()s the selected radio app
# binary (sdr_app / adsb_app / meshtastic_app), handing off and reclaiming the
# single display. See apps/radio/README.md.
# ====================================================
file(GLOB_RECURSE RADIO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_executable(radio_app ${RADIO_SOURCES})

target_include_directories(radio_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/launch
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)

target_link_libraries(radio_app
    PRIVATE
        radio_toolkit
        lvgl
)
