# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Development-only UI test automation (WHALE_ENABLE_DEV_MCP), see README.md.
# Whale offers it on D-Bus when started with WHALE_DEV_MCP=1; adapter/whale_dev_mcp.py
# turns it into an MCP server. Nothing of it is installed.

add_library(whaledevautomation STATIC)

target_sources(whaledevautomation PRIVATE
    automation/whaledevautomation.cpp
    automation/whaledevautomationdbus.cpp
    automation/whaledevtoolbars.cpp
)

target_include_directories(whaledevautomation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/automation)

target_link_libraries(whaledevautomation PUBLIC
    dolphinstatic
    whaleviewhost
    Qt6::DBus
)

# The executable (defined in src/) starts the automation from main().
target_link_libraries(dolphin PRIVATE whaledevautomation)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
