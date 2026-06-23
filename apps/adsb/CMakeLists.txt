#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

# ====================================================
# nlohmann/json — header-only, used by the ADS-B parser
# ====================================================
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found in system; fetching v3.11.3")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ====================================================
# adsb_app — the ADS-B viewer executable
# ====================================================
file(GLOB_RECURSE ADSB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

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
        APP_MOCK_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/mock/aircraft.json"
)

target_link_libraries(adsb_app
    PRIVATE
        radio_toolkit
        lvgl
        nlohmann_json::nlohmann_json
)
