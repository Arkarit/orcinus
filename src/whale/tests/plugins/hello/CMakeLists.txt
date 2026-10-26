# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# A test view: built for the tests only, into the test plugin directory, never installed.
configure_file(whale_hello.json.in ${CMAKE_CURRENT_BINARY_DIR}/whale_hello.json @ONLY)
# AutoMoc regenerates the included .moc file when the JSON changes, but Ninja only notices that
# in the next build. Recompile directly, so a new API version is never built in one build late.
set_source_files_properties(whalehelloplugin.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/whale_hello.json)

# A plain module instead of kcoreaddons_add_plugin, because that would install it.
add_library(whale_hello MODULE whalehelloplugin.cpp)
target_link_libraries(whale_hello PRIVATE whaleviewapi KF6::CoreAddons Qt6::Widgets)
set_target_properties(whale_hello PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${WHALE_TEST_VIEW_PLUGIN_DIR}/orcinus/views)
