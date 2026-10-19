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
# sdr_core — the receiver logic with no LVGL, audio or device dependency
# (squelch, channel level, memory scan, capture rate, zoom crop), so unit tests link it on its own.
add_library(sdr_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sdr/squelch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sdr/memory_scan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sdr/sample_rate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sdr/spectrum_crop.cpp
)
target_include_directories(sdr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/sdr)

if(USE_DESKTOP)
    add_executable(squelch_test ${CMAKE_CURRENT_SOURCE_DIR}/test/squelch_test.cpp)
    target_link_libraries(squelch_test PRIVATE sdr_core)
    add_test(NAME squelch_test COMMAND squelch_test)
    add_executable(memory_scan_test ${CMAKE_CURRENT_SOURCE_DIR}/test/memory_scan_test.cpp)
    target_link_libraries(memory_scan_test PRIVATE sdr_core)
    add_test(NAME memory_scan_test COMMAND memory_scan_test)
    add_executable(sample_rate_test ${CMAKE_CURRENT_SOURCE_DIR}/test/sample_rate_test.cpp)
    target_link_libraries(sample_rate_test PRIVATE sdr_core)
    add_test(NAME sample_rate_test COMMAND sample_rate_test)
    add_executable(spectrum_crop_test ${CMAKE_CURRENT_SOURCE_DIR}/test/spectrum_crop_test.cpp)
    target_link_libraries(spectrum_crop_test PRIVATE sdr_core)
    add_test(NAME spectrum_crop_test COMMAND spectrum_crop_test)
endif()

file(GLOB_RECURSE SDR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
# These live in sdr_core (linked below): don't compile them twice. Filter by
# regex, as adsb_app does, so a differently normalised glob result cannot slip
# a second copy in.
list(FILTER SDR_SOURCES EXCLUDE REGEX "/sdr/(squelch|memory_scan|sample_rate|spectrum_crop)\\.cpp$")

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
        sdr_core
)

# ----------------------------------------------------------------------
# RTL-SDR backend (live receiver). Two independent capabilities:
#   SDR_HAVE_RTLTCP — the rtl_tcp spectrum/waterfall source (needs fftw3f +
#       POSIX sockets only). Enabled on desktop AND on the device cross build,
#       so the CardputerZero can render real RF from an rtl_tcp server over the
#       network (capture on host, view on device).
#   SDR_HAVE_AUDIO  — the audio demodulator: SDL2 on desktop, ALSA on the
#       device (SDR_HAVE_ALSA, plays out the ES8388 speaker via PipeWire).
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
        # Device build: ALSA audio (the demodulator plays out the ES8388 speaker)
        # when libasound is available in the sysroot.
        find_library(ASOUND_LIBRARY asound)
        if(ASOUND_LIBRARY)
            target_compile_definitions(sdr_app PRIVATE SDR_HAVE_AUDIO SDR_HAVE_ALSA)
            target_link_libraries(sdr_app PRIVATE ${ASOUND_LIBRARY})
            message(STATUS "sdr_app: RTL-SDR backend ENABLED (fftw3f + ALSA audio)")
        else()
            message(STATUS "sdr_app: RTL-SDR spectrum backend ENABLED (fftw3f, no audio)")
        endif()
    endif()
else()
    message(STATUS "sdr_app: RTL-SDR backend disabled (mock source only)")
endif()

# ----------------------------------------------------------------------
# sdr_model_test — the frequency correction math and the state file
# migration (host-only, registered with CTest). SdrModel has no LVGL or
# radio dependency, so it builds on its own against radio_toolkit.
# ----------------------------------------------------------------------
if(USE_DESKTOP)
    add_executable(sdr_model_test
        ${CMAKE_CURRENT_SOURCE_DIR}/test/sdr_model_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model/sdr_model.cpp)
    target_include_directories(sdr_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/model)
    target_link_libraries(sdr_model_test PRIVATE radio_toolkit)
    add_test(NAME sdr_model_test COMMAND sdr_model_test)
endif()
