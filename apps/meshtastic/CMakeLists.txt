#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

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
)

target_link_libraries(meshtastic_app
    PRIVATE
        radio_toolkit
        lvgl
)
