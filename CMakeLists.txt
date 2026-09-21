#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.31)

project(cardputer_radio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# CMake configure options
# USE_DESKTOP: whether to build the desktop emulator (LVGL SDL simulator).
# For the device build, configure with -DUSE_DESKTOP=OFF (the cp0-cross preset).
set(USE_DESKTOP ON CACHE BOOL "Build the desktop emulator target with LVGL SDL support")
set(APP_KEY_INPUT_DEVICE "" CACHE STRING "Optional Linux evdev keyboard/keypad device path, e.g. /dev/input/event0")
set(APP_FRAMEBUFFER_DEVICE "/dev/fb0" CACHE STRING "Linux framebuffer device path used by embedded builds")
set(APP_DRM_DEVICE "/dev/dri/card0" CACHE STRING "Linux DRM device path used when APP_USE_DRM=ON")
set(APP_DRM_CONNECTOR_ID "-1" CACHE STRING "DRM connector id used when APP_USE_DRM=ON; -1 auto-selects")
set(APP_USE_DRM OFF CACHE BOOL "Enable LVGL Linux DRM/KMS backend for embedded Linux builds")
set(CM0_ALLOW_FETCH_DEPS ON CACHE BOOL "Allow downloading missing CM0 BSP/dependencies during cross builds")
# Export compile commands for IDE IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

# Shared assets root (fonts) the toolkit's AssetManager resolves at runtime.
set(RADIO_ASSETS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets")

include(FetchContent)

# ====================================================
# lvgl - main ui framework
# ====================================================
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/toolkit/src/config/lv_conf.h" CACHE PATH "" FORCE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(lvgl)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()
add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(LVGL_SOURCE_DIR "${lvgl_SOURCE_DIR}")
get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_SOURCE_DIR}" DIRECTORY)

# ====================================================
# other misc dependencies
# ====================================================
if(USE_DESKTOP)
    message(STATUS "Configuring DESKTOP build")

    find_package(PNG REQUIRED)
    find_package(JPEG REQUIRED)

    if(TARGET libjpeg-turbo::libjpeg-turbo)
        set(JPEG_TARGET libjpeg-turbo::libjpeg-turbo)
    elseif(TARGET JPEG::JPEG)
        set(JPEG_TARGET JPEG::JPEG)
    else()
        message(FATAL_ERROR "No JPEG provider found")
    endif()

    find_package(Freetype REQUIRED)
    find_package(SDL2 CONFIG REQUIRED)
    find_package(fmt CONFIG REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(Threads REQUIRED)

    set(LVGL_DESKTOP_DEPS
        ZLIB::ZLIB
        PNG::PNG
        ${JPEG_TARGET}
        Freetype::Freetype
        SDL2::SDL2
    )

    foreach(dep IN LISTS LVGL_DESKTOP_DEPS)
        if(TARGET ${dep})
            target_link_libraries(lvgl PUBLIC ${dep})
        endif()
    endforeach()

    if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
    endif()

    target_compile_definitions(lvgl PUBLIC USE_DESKTOP=1)

    if(MSVC)
        target_compile_options(lvgl PUBLIC /utf-8)
    endif()

# Cross builds: search individual dependencies from the BSP sysroot.
else()
    message(STATUS "Configuring CM0/aarch64 Linux build")

    if(NOT CM0_SDK_ROOT OR NOT EXISTS "${CM0_SDK_ROOT}")
        message(FATAL_ERROR
            "CM0 SDK sysroot not found or invalid.\n"
            "The cp0 toolchain should download the BSP before project() runs, "
            "or configure with -DCM0_SDK_ROOT=<path_to_sdk_sysroot>."
        )
    endif()

    if(CMAKE_SYSROOT)
        set(CM0_SYSROOT "${CMAKE_SYSROOT}")
    else()
        set(CM0_SYSROOT "${CM0_SDK_ROOT}")
    endif()
    if(NOT CM0_MULTIARCH)
        set(CM0_MULTIARCH "aarch64-linux-gnu")
    endif()
    message(STATUS "Using CM0 SDK root: ${CM0_SDK_ROOT}")
    message(STATUS "Using CM0 dependency root: ${CM0_SYSROOT}")

    set(CM0_INCLUDE_HINTS
        "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/include"
        "${CM0_SYSROOT}/usr/local/include"
        "${CM0_SYSROOT}/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/include"
    )
    set(CM0_LIBRARY_HINTS
        "${CM0_SYSROOT}/usr/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/lib"
        "${CM0_SYSROOT}/usr/local/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/local/lib"
        "${CM0_SYSROOT}/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/lib"
    )

    function(cm0_find_header out_var header_name)
        find_path(${out_var}
            NAMES ${header_name}
            HINTS ${ARGN}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_find_library out_var)
        find_library(${out_var}
            NAMES ${ARGN}
            HINTS ${CM0_LIBRARY_HINTS}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    function(cm0_add_imported_library target_name library_path include_dirs)
        if(NOT TARGET ${target_name})
            add_library(${target_name} UNKNOWN IMPORTED)
            set_target_properties(${target_name} PROPERTIES
                IMPORTED_LOCATION "${library_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_dirs}"
            )
        endif()
    endfunction()

    if(APP_USE_DRM)
        cm0_find_header(DRM_XF86_INCLUDE_DIR xf86drm.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_XF86_MODE_INCLUDE_DIR xf86drmMode.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_MODE_INCLUDE_DIR drm.h
            "${CM0_SYSROOT}/usr/include/drm"
            "${CM0_SYSROOT}/usr/include/libdrm"
            "${CM0_SYSROOT}/usr/include"
        )
        cm0_find_library(DRM_LIBRARY drm drm.so.2)

        if(NOT DRM_XF86_INCLUDE_DIR
           OR NOT DRM_XF86_MODE_INCLUDE_DIR
           OR NOT DRM_MODE_INCLUDE_DIR
           OR NOT DRM_LIBRARY)
            message(FATAL_ERROR
                "APP_USE_DRM=ON but libdrm headers/library were not found in BSP sysroot.\n"
                "Provide them in ${CM0_SYSROOT} or disable with -DAPP_USE_DRM=OFF."
            )
        endif()
        cm0_add_imported_library(CM0::DRM "${DRM_LIBRARY}"
            "${DRM_XF86_INCLUDE_DIR};${DRM_XF86_MODE_INCLUDE_DIR};${DRM_MODE_INCLUDE_DIR}"
        )
        message(STATUS "CM0 DRM display support enabled")
    endif()

    cm0_find_header(FREETYPE_INCLUDE_DIR ft2build.h
        "${CM0_SYSROOT}/usr/include/freetype2"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FREETYPE_LIBRARY freetype)
    if(NOT FREETYPE_INCLUDE_DIR OR NOT FREETYPE_LIBRARY)
        message(FATAL_ERROR "Missing Freetype header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::Freetype "${FREETYPE_LIBRARY}" "${FREETYPE_INCLUDE_DIR}")

    cm0_find_header(PNG_INCLUDE_DIR png.h
        "${CM0_SYSROOT}/usr/include/libpng16"
        "${CM0_SYSROOT}/usr/include/libpng"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(PNG_LIBRARY png16 png)
    if(NOT PNG_INCLUDE_DIR OR NOT PNG_LIBRARY)
        message(FATAL_ERROR "Missing PNG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::PNG "${PNG_LIBRARY}" "${PNG_INCLUDE_DIR}")

    cm0_find_header(JPEG_INCLUDE_DIR jpeglib.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(JPEG_LIBRARY jpeg)
    if(NOT JPEG_INCLUDE_DIR OR NOT JPEG_LIBRARY)
        message(FATAL_ERROR "Missing JPEG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::JPEG "${JPEG_LIBRARY}" "${JPEG_INCLUDE_DIR}")

    cm0_find_header(ZLIB_INCLUDE_DIR zlib.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(ZLIB_LIBRARY z)
    if(NOT ZLIB_INCLUDE_DIR OR NOT ZLIB_LIBRARY)
        message(FATAL_ERROR "Missing ZLIB header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(ZLIB::ZLIB "${ZLIB_LIBRARY}" "${ZLIB_INCLUDE_DIR}")

    set(CM0_MULTIARCH_INCLUDE_DIRS)
    foreach(CM0_INCLUDE_DIR IN ITEMS
            "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
            "${CM0_SYSROOT}/include/${CM0_MULTIARCH}")
        if(EXISTS "${CM0_INCLUDE_DIR}/bits/timesize.h" OR EXISTS "${CM0_INCLUDE_DIR}/bits/wordsize.h")
            list(APPEND CM0_MULTIARCH_INCLUDE_DIRS "${CM0_INCLUDE_DIR}")
            message(STATUS "Found multiarch include: ${CM0_INCLUDE_DIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES CM0_MULTIARCH_INCLUDE_DIRS)

    cm0_find_header(FMT_INCLUDE_DIR fmt/format.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FMT_LIBRARY fmt libfmt.so.10 libfmt.so.10.1.0)
    if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
        cm0_add_imported_library(fmt::fmt "${FMT_LIBRARY}" "${FMT_INCLUDE_DIR}")
    elseif(CM0_ALLOW_FETCH_DEPS)
        message(STATUS "Missing fmt in BSP sysroot, fetching from GitHub because CM0_ALLOW_FETCH_DEPS=ON")
        FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG        10.1.1
            GIT_SHALLOW    TRUE
        )
        set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules for app packages" FORCE)
        set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
        set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
        FetchContent_MakeAvailable(fmt)
        if(TARGET fmt)
            target_include_directories(fmt SYSTEM BEFORE PRIVATE ${CM0_MULTIARCH_INCLUDE_DIRS})
        endif()
        if(TARGET fmt AND NOT TARGET fmt::fmt)
            add_library(fmt::fmt ALIAS fmt)
        endif()
    else()
        message(FATAL_ERROR
            "Missing fmt header/library in BSP sysroot: ${CM0_SYSROOT}\n"
            "Install/copy fmt into the BSP, or reconfigure with -DCM0_ALLOW_FETCH_DEPS=ON."
        )
    endif()

    set_target_properties(CM0::PNG PROPERTIES INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
    set_target_properties(CM0::Freetype PROPERTIES INTERFACE_LINK_LIBRARIES "CM0::PNG;ZLIB::ZLIB")

    cm0_find_library(PTHREAD_LIBRARY pthread libpthread.so.0 libpthread.a)
    cm0_find_library(M_LIBRARY m libm.so libm.so.6 libm.a)
    if(NOT PTHREAD_LIBRARY)
        message(FATAL_ERROR "Missing pthread library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    if(NOT M_LIBRARY)
        message(FATAL_ERROR "Missing math library in BSP sysroot: ${CM0_SYSROOT}")
    endif()

    set(PLATFORM_INCLUDE_DIR ${CM0_MULTIARCH_INCLUDE_DIRS})
    set(PLATFORM_LIBS ${PTHREAD_LIBRARY} ${M_LIBRARY})

    set(LVGL_INCLUDE_DIR
        ${FREETYPE_INCLUDE_DIR}
        ${PNG_INCLUDE_DIR}
        ${JPEG_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}
    )
    set(LVGL_LIBS
        CM0::Freetype
        CM0::PNG
        CM0::JPEG
        ZLIB::ZLIB
    )
    if(APP_USE_DRM)
        list(APPEND LVGL_INCLUDE_DIR
            ${DRM_XF86_INCLUDE_DIR}
            ${DRM_XF86_MODE_INCLUDE_DIR}
            ${DRM_MODE_INCLUDE_DIR}
        )
        list(APPEND LVGL_LIBS CM0::DRM)
    endif()

    list(REMOVE_DUPLICATES PLATFORM_INCLUDE_DIR)
    list(REMOVE_DUPLICATES PLATFORM_LIBS)
    list(REMOVE_DUPLICATES LVGL_INCLUDE_DIR)
    list(REMOVE_DUPLICATES LVGL_LIBS)

    target_compile_definitions(lvgl
        PUBLIC
            USE_DESKTOP=0
            APP_USE_DRM=$<BOOL:${APP_USE_DRM}>
    )

    # fix: issue of compiling lvgl on gcc >= 15.0
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
            target_compile_options(lvgl PRIVATE -Wno-stringop-overflow)
        endif()
    endif()

    target_include_directories(lvgl
        PUBLIC
            ${LVGL_INCLUDE_DIR}
            ${PLATFORM_INCLUDE_DIR}
    )

    target_link_libraries(lvgl
        PUBLIC
            ${LVGL_LIBS}
            ${PLATFORM_LIBS}
    )

endif()

# ====================================================
# logger (fmt-based)
# ====================================================
file(GLOB_RECURSE LOGGER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/logger/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/logger/*.cpp"
)

add_library(logger STATIC ${LOGGER_SOURCES})
target_include_directories(logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/logger
    ${PLATFORM_INCLUDE_DIR}
)
target_link_libraries(logger
    PUBLIC
        fmt::fmt
    PRIVATE
        ${PLATFORM_LIBS}
)

# ====================================================
# radio_toolkit — the reusable static library
# Bundles the reusable app shell (app/reactive/platform/view/widgets) plus the
# new shared radio components (geo/model/net).
# ====================================================
file(GLOB_RECURSE TOOLKIT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/reactive/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/platform/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/view/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/geo/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/map/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/model/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/net/*.cpp"
)

# radio_toolkit uses std::thread (file_json_source, future net sources), so it
# needs the platform threading library on every target (not just desktop).
find_package(Threads REQUIRED)

add_library(radio_toolkit STATIC ${TOOLKIT_SOURCES})

target_include_directories(radio_toolkit
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${PLATFORM_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/config
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/logger
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/reactive
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/view
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/view/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/view/screens
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/geo
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/map
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/src/net
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/remote-fb
)

# The AssetManager bakes in the app name and the shared assets root (fonts).
# The device build resolves assets from the installed package only, so no
# build-machine path ends up in the shipped binaries.
if(CMAKE_CROSSCOMPILING)
    set(RADIO_RUNTIME_ASSETS_ROOT "/usr/share/zeroradio")
else()
    set(RADIO_RUNTIME_ASSETS_ROOT "${RADIO_ASSETS_ROOT}")
endif()
target_compile_definitions(radio_toolkit
    PUBLIC
        APP_NAME="zeroradio"
        APP_VERSION="${PROJECT_VERSION}"
        APP_CMAKE_ASSETS_ROOT="${RADIO_RUNTIME_ASSETS_ROOT}"
        APP_SOURCE_ASSETS_ROOT="${RADIO_RUNTIME_ASSETS_ROOT}"
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
        APP_KEY_INPUT_DEVICE="${APP_KEY_INPUT_DEVICE}"
)

target_link_libraries(radio_toolkit
    PUBLIC
        lvgl
        logger
        Threads::Threads
)

# ====================================================
# apps
# ====================================================
# Host-side unit tests (e.g. the AIS decoder parity test) register with CTest.
enable_testing()

# Toolkit unit tests (host-only): the shared waterfall primitive + colormap.
if(USE_DESKTOP)
    add_executable(waterfall_scroll_test
        ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/test/waterfall_scroll_test.cpp)
    target_link_libraries(waterfall_scroll_test PRIVATE radio_toolkit)
    add_test(NAME waterfall_scroll_test COMMAND waterfall_scroll_test)
endif()

# Base-map renderer: culling must not change a pixel; also times a frame, so the
# cross-built binary benchmarks a dataset on the device (argv[1] = .rmap path).
add_executable(map_render_test ${CMAKE_CURRENT_SOURCE_DIR}/toolkit/test/map_render_test.cpp)
target_link_libraries(map_render_test PRIVATE radio_toolkit)
target_compile_definitions(map_render_test PRIVATE
    MAP_TEST_DEFAULT="${RADIO_ASSETS_ROOT}/mapdata/world.rmap")
if(USE_DESKTOP)
    add_test(NAME map_render_test COMMAND map_render_test)
endif()

# nlohmann/json — header-only, used by the ADS-B and ISM parsers. Resolved here,
# not in an app subdirectory: a system package yields an IMPORTED target that is
# only visible in the directory that found it (the FetchContent fallback is global).
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

add_subdirectory(apps/adsb)
add_subdirectory(apps/sdr)
add_subdirectory(apps/meshtastic)
add_subdirectory(apps/radio)
add_subdirectory(apps/ais)
add_subdirectory(apps/survey)
add_subdirectory(apps/ism)

# ====================================================
# Packaging: the `zeroradio` Debian package (ZeroRadio in the AppStore)
# ====================================================
# Lays out everything under /usr/share/zeroradio (the hub + app binaries, the
# bundled decoders, the launch wrapper, and the runtime assets the AssetManager
# resolves from /usr/share/<app_name>), plus the single APPLaunch entry + icon.
# Installing it on a stock CardputerZero (AppStore, or `apt install ./x.deb`)
# pulls every Debian dependency, so nothing has to be installed by hand.
# Only meaningful for the aarch64 cross build; skipped for desktop/SDL builds.
if(CMAKE_CROSSCOMPILING)
    set(RADIO_PKG_BINDIR  "share/zeroradio/bin")
    set(RADIO_PKG_DATADIR "share/zeroradio")
    set(RADIO_APPLAUNCH   "share/APPLaunch")

    # Bundled decoders (built from source, see cmake/decoders.cmake).
    include(cmake/decoders.cmake)
    install(PROGRAMS "${RADIO_READSB_BIN}" "${RADIO_AISCATCHER_BIN}" DESTINATION "${RADIO_PKG_BINDIR}")

    # App binaries + the hub, colocated so the hub can posix_spawn() its siblings.
    # Meshtastic is not shipped yet: it needs meshtasticd, a root system service
    # from a third-party repo, plus the optional Cap LoRa hardware. The hub hides
    # apps whose binary is absent.
    install(TARGETS radio_app sdr_app adsb_app ais_app survey_app ism_app
            RUNTIME DESTINATION "${RADIO_PKG_BINDIR}")

    # Launch wrapper — carries the SDR runtime env (sample-rate cap, pipewire audio).
    install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/apps/radio/applications/radio-launch.sh"
            DESTINATION "${RADIO_PKG_BINDIR}")

    # Runtime assets — resolved via the AssetManager root /usr/share/zeroradio.
    install(DIRECTORY "${RADIO_ASSETS_ROOT}/fonts" "${RADIO_ASSETS_ROOT}/mapdata"
            DESTINATION "${RADIO_PKG_DATADIR}")
    # Sample data for the *_SOURCE=mock modes (demos, screenshots).
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/apps/adsb/assets/mock/aircraft.json"
                  "${CMAKE_CURRENT_SOURCE_DIR}/apps/ais/assets/mock/ais.nmea"
            DESTINATION "${RADIO_PKG_DATADIR}/mock")

    # APPLaunch integration: the single launcher entry + its icon.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/apps/radio/applications/zeroradio.desktop"
            DESTINATION "${RADIO_APPLAUNCH}/applications")
    install(FILES "${RADIO_ASSETS_ROOT}/images/zeroradio.png"
            DESTINATION "${RADIO_APPLAUNCH}/share/images")

    # ---- CPack: Debian package ----
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "zeroradio")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_CONTACT "Andrea Salvatore <andreahaku@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "SDR, ADS-B, AIS, spectrum survey and ISM radio tools for CardputerZero")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/andreahaku/cardputer-radio")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
    # Shared-library dependencies of every shipped binary (apps + bundled
    # decoders) are computed by dpkg-shlibdeps against the trixie arm64 libs.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # Tools the apps spawn, from Debian: rtl_tcp/rtl_power (rtl-sdr), rtl_433
    # (rtl-433), hackrf_sweep (hackrf). pipewire-alsa routes the SDR audio
    # through PipeWire (docs/sdr-device-profile.md).
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "rtl-sdr, rtl-433, hackrf, pipewire-alsa")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    include(CPack)
endif()
