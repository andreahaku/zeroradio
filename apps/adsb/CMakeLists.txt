#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# adsb_decoder — the pure dump1090 aircraft.json parser (parse_aircraft_json)
# plus its EntityStore projection. Split into a static lib so the parse path has
# a standalone parity test. See apps/adsb/README.md.
# ====================================================
add_library(adsb_decoder STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/aircraft.cpp
)
target_include_directories(adsb_decoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/model)
target_link_libraries(adsb_decoder PUBLIC radio_toolkit nlohmann_json::nlohmann_json)

# ----------------------------------------------------------------------
# aircraft_parse_test — parity test against the frozen dump1090 JSON oracle.
# Host-only (desktop build); registered with CTest.
# ----------------------------------------------------------------------
if(USE_DESKTOP)
    add_executable(aircraft_parse_test ${CMAKE_CURRENT_SOURCE_DIR}/test/aircraft_parse_test.cpp)
    target_link_libraries(aircraft_parse_test PRIVATE adsb_decoder)
    add_test(NAME aircraft_parse_test COMMAND aircraft_parse_test)
endif()

# ====================================================
# adsb_app — the ADS-B viewer executable
# ====================================================
file(GLOB_RECURSE ADSB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
# aircraft.cpp now lives in adsb_decoder (linked below) — don't compile it twice.
# Filter by regex rather than an exact path so a differently-normalized glob
# result (symlinks, separators) can't slip a second copy into adsb_app.
list(FILTER ADSB_SOURCES EXCLUDE REGEX "/model/aircraft\\.cpp$")

add_executable(adsb_app ${ADSB_SOURCES})

target_include_directories(adsb_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
        ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)

# Default mock JSON path (absolute, in the monorepo source tree). ADSB_JSON env
# at runtime overrides it.
target_compile_definitions(adsb_app
    PRIVATE
        APP_MOCK_JSON_PATH="$<IF:$<BOOL:${CMAKE_CROSSCOMPILING}>,/usr/share/zeroradio/mock/aircraft.json,${CMAKE_CURRENT_SOURCE_DIR}/assets/mock/aircraft.json>"
)

target_link_libraries(adsb_app
    PRIVATE
        adsb_decoder
        radio_toolkit
        lvgl
        nlohmann_json::nlohmann_json
)
