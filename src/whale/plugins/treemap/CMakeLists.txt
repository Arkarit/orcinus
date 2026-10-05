# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

configure_file(whale_treemap.json.in ${CMAKE_CURRENT_BINARY_DIR}/whale_treemap.json @ONLY)
# See plugins/testlist: recompile directly when the metadata changes.
set_source_files_properties(whaletreemapplugin.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/whale_treemap.json)

# The layout and the renderer do not depend on the view; the layout test uses them directly.
add_library(whaletreemapcore STATIC treemaplayout.cpp treemaprenderer.cpp)
set_target_properties(whaletreemapcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(whaletreemapcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(whaletreemapcore PUBLIC whaleviewapi Qt6::Gui)

kcoreaddons_add_plugin(whale_treemap SOURCES whaletreemapplugin.cpp treemapview.cpp INSTALL_NAMESPACE "whale/views")
target_link_libraries(whale_treemap PRIVATE whaletreemapcore whaleviewapi KF6::CoreAddons KF6::I18n Qt6::Widgets)
set_target_properties(whale_treemap PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${WHALE_VIEW_PLUGIN_BUILD_DIR}/whale/views)
