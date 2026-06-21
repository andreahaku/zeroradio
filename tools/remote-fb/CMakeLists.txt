#
# SPDX-License-Identifier: MIT
#
# remote-fb desktop tools (Path B): a standalone viewer that shows a remote
# headless cardputer-radio app's framebuffer and forwards the X1 keyboard back
# to it, plus a mock streamer + headless self-test for development without a Pi.
#
# These are HOST tools (built on the X1), independent of the LVGL app build.
# Build standalone:
#   cmake -S tools/remote-fb -B tools/remote-fb/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build tools/remote-fb/build
#
cmake_minimum_required(VERSION 3.16)
project(remote_fb_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Mock + self-test have no GUI dependency.
add_executable(remote-fb-mock mock_streamer.cpp)
target_link_libraries(remote-fb-mock PRIVATE Threads::Threads)

add_executable(remote-fb-selftest selftest.cpp)

# The viewer needs SDL2; skip it gracefully if SDL2 is absent.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    add_executable(remote-fb-viewer viewer.cpp)
    target_link_libraries(remote-fb-viewer PRIVATE SDL2::SDL2 Threads::Threads)
    message(STATUS "remote-fb: building viewer (SDL2 ${SDL2_VERSION})")
else()
    message(WARNING "remote-fb: SDL2 not found, viewer target skipped (mock/selftest still build)")
endif()
