#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# sdr_app — the SDR receiver executable (ported from SDRTerminal)
# Scaffold step: shell + stub screen reusing radio_toolkit. The spectrum/
# waterfall pipeline, RTL-SDR source (FFTW3f + Threads) and audio demod are
# added in later steps. See docs/sdrterminal-port-plan.md.
# ====================================================
file(GLOB_RECURSE SDR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_executable(sdr_app ${SDR_SOURCES})

target_include_directories(sdr_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sdr
)

target_link_libraries(sdr_app
    PRIVATE
        radio_toolkit
        lvgl
)
