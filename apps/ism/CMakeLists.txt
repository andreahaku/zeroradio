#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# ism_decoder — the pure rtl_433 JSON-line parser (parse_ism_json_line) plus
# its EntityStore projection. A static lib so the parse path has a standalone
# parity test (same pattern as adsb_decoder / ais_decoder). See README.md.
# ====================================================
add_library(ism_decoder STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/ism_reading.cpp
)
target_include_directories(ism_decoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/model)
target_link_libraries(ism_decoder PUBLIC radio_toolkit nlohmann_json::nlohmann_json)

# ----------------------------------------------------------------------
# ism_parse_test — parity test against the frozen two-source oracle
# (python gen_ism_vectors.py). Host-only; registered with CTest.
# ----------------------------------------------------------------------
if(USE_DESKTOP)
    add_executable(ism_parse_test ${CMAKE_CURRENT_SOURCE_DIR}/test/ism_parse_test.cpp)
    target_include_directories(ism_parse_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
    target_link_libraries(ism_parse_test PRIVATE ism_decoder)
    add_test(NAME ism_parse_test COMMAND ism_parse_test)
endif()

# ====================================================
# ism_app — the ISM device sniffer executable. rtl_433 -F json feeds decoded
# readings into the shared EntityStore; the screen lists them (List / Detail /
# Settings, no radar — ISM devices have no position). See README.md.
# ====================================================
file(GLOB_RECURSE ISM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
# ism_reading.cpp lives in ism_decoder (linked below) — don't compile it twice.
list(FILTER ISM_SOURCES EXCLUDE REGEX "/model/ism_reading\\.cpp$")

add_executable(ism_app ${ISM_SOURCES})

target_include_directories(ism_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/source
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)

target_link_libraries(ism_app
    PRIVATE
        ism_decoder
        radio_toolkit
        lvgl
        nlohmann_json::nlohmann_json
)

# Live source: fork/exec of rtl_433 + a worker thread (toolkit::Subprocess).
# POSIX-only; without it the app builds and runs on the mock/file source.
if(UNIX)
    target_compile_definitions(ism_app PRIVATE ISM_HAVE_SOURCE)
    target_link_libraries(ism_app PRIVATE Threads::Threads)
    message(STATUS "ism_app: live rtl_433 source ENABLED")
else()
    message(STATUS "ism_app: live rtl_433 source disabled (mock/file only)")
endif()
