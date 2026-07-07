#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# survey_sweep — pure sweep CSV parser/accumulator (rtl_power / hackrf_sweep
# stdout -> full-range dBm frames + peaks). No LVGL/subprocess deps, so it
# links into a standalone unit test against frozen vectors.
# See src/sweep/sweep_accumulator.h for the parsing contract.
# ====================================================
add_library(survey_sweep STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sweep/sweep_accumulator.cpp
)
target_include_directories(survey_sweep PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/sweep)

# ----------------------------------------------------------------------
# sweep_parser_test — parity test against frozen, two-source sweep vectors.
# Host-only (desktop build); registered with CTest.
# ----------------------------------------------------------------------
if(USE_DESKTOP)
    add_executable(sweep_parser_test ${CMAKE_CURRENT_SOURCE_DIR}/test/sweep_parser_test.cpp)
    target_link_libraries(sweep_parser_test PRIVATE survey_sweep)
    add_test(NAME sweep_parser_test COMMAND sweep_parser_test)
endif()

# ====================================================
# survey_app — the Spectrum Survey executable (radio-apps 10a). A wide-band
# waterfall + PEAKS list fed by rtl_power / hackrf_sweep through the pure
# SweepAccumulator; peaks hand off to the SDR app (SDR_FREQ). See README.md.
# ====================================================
file(GLOB_RECURSE SURVEY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(survey_app ${SURVEY_SOURCES})

target_include_directories(survey_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sweep
)

target_link_libraries(survey_app
    PRIVATE
        radio_toolkit
        lvgl
)

# Live sweep backend: fork/exec of rtl_power / hackrf_sweep + a worker thread.
# POSIX-only; without it the app builds and runs on the synthetic mock source.
if(UNIX)
    target_compile_definitions(survey_app PRIVATE SURVEY_HAVE_SWEEP)
    target_link_libraries(survey_app PRIVATE Threads::Threads)
    message(STATUS "survey_app: live sweep backend ENABLED (rtl_power / hackrf_sweep)")
else()
    message(STATUS "survey_app: live sweep backend disabled (mock source only)")
endif()
