#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# sdr_app — the SDR receiver executable (ported from SDRTerminal)
# Spectrum/waterfall pipeline, RTL-SDR source (FFTW3f + Threads) and audio demod,
# built on radio_toolkit. See apps/sdr/README.md.
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
# RTL-SDR backend (live receiver). Two independent capabilities:
#   SDR_HAVE_RTLTCP — the rtl_tcp spectrum/waterfall source (needs fftw3f +
#       POSIX sockets only). Enabled on desktop AND on the device cross build,
#       so the CardputerZero can render real RF from an rtl_tcp server over the
#       network (capture on host, view on device).
#   SDR_HAVE_AUDIO  — the SDL2 audio demodulator. Desktop only for now; the
#       device gets a no-op AudioDemod (audio is deferred to a future ALSA port).
# Without fftw3f, sdr_app still builds and runs on the synthetic mock source.
# ----------------------------------------------------------------------
find_library(FFTW3F_LIBRARY fftw3f)
if(FFTW3F_LIBRARY)
    target_compile_definitions(sdr_app PRIVATE SDR_HAVE_RTLTCP)
    target_link_libraries(sdr_app PRIVATE ${FFTW3F_LIBRARY} Threads::Threads)
    if(USE_DESKTOP AND TARGET SDL2::SDL2)
        target_compile_definitions(sdr_app PRIVATE SDR_HAVE_AUDIO)
        target_link_libraries(sdr_app PRIVATE SDL2::SDL2)
        message(STATUS "sdr_app: RTL-SDR backend ENABLED (fftw3f + SDL2 audio)")
    else()
        message(STATUS "sdr_app: RTL-SDR spectrum backend ENABLED (fftw3f, no audio)")
    endif()
else()
    message(STATUS "sdr_app: RTL-SDR backend disabled (mock source only)")
endif()
