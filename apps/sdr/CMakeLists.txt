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

# ----------------------------------------------------------------------
# RTL-SDR backend (live receiver): the rtl_tcp source needs fftw3f for the
# FFT and SDL2 audio for the demodulator. Enabled only when both are present
# (desktop). Without it, sdr_app still builds and runs on the mock source.
# ----------------------------------------------------------------------
find_library(FFTW3F_LIBRARY fftw3f)
if(USE_DESKTOP AND FFTW3F_LIBRARY AND TARGET SDL2::SDL2)
    target_compile_definitions(sdr_app PRIVATE SDR_HAVE_RTLTCP)
    target_link_libraries(sdr_app PRIVATE ${FFTW3F_LIBRARY} SDL2::SDL2 Threads::Threads)
    message(STATUS "sdr_app: RTL-SDR backend ENABLED (fftw3f + SDL2)")
else()
    message(STATUS "sdr_app: RTL-SDR backend disabled (mock source only)")
endif()
