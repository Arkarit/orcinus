# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

configure_file(whale_hello.json.in ${CMAKE_CURRENT_BINARY_DIR}/whale_hello.json @ONLY)
# AutoMoc regenerates the included .moc file when the JSON changes, but Ninja only notices that
# in the next build. Recompile directly, so a new API version is never built in one build late.
set_source_files_properties(whalehelloplugin.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/whale_hello.json)

kcoreaddons_add_plugin(whale_hello SOURCES whalehelloplugin.cpp INSTALL_NAMESPACE "whale/views")
target_link_libraries(whale_hello PRIVATE whaleviewapi KF6::CoreAddons Qt6::Widgets)
set_target_properties(whale_hello PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${WHALE_VIEW_PLUGIN_BUILD_DIR}/whale/views)
