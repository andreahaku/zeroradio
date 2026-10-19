#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#

add_executable(memories_app
    src/main.cpp
    src/viewmodel/memories_viewmodel.cpp
    src/view/memories_screen.cpp
)

target_include_directories(memories_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view
)

target_link_libraries(memories_app PRIVATE radio_toolkit)
