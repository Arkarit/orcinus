# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

configure_file(orcinus_treemap.json.in ${CMAKE_CURRENT_BINARY_DIR}/orcinus_treemap.json @ONLY)
# See tests/plugins/testlist: recompile directly when the metadata changes.
set_source_files_properties(whaletreemapplugin.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/orcinus_treemap.json)

# The layout and the renderer do not depend on the view; the layout test uses them directly.
add_library(whaletreemapcore STATIC treemaplayout.cpp treemaprenderer.cpp)
set_target_properties(whaletreemapcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(whaletreemapcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(whaletreemapcore PUBLIC whaleviewapi Qt6::Gui)

kcoreaddons_add_plugin(orcinus_treemap SOURCES whaletreemapplugin.cpp treemapview.cpp INSTALL_NAMESPACE "orcinus/views")
target_link_libraries(orcinus_treemap PRIVATE whaletreemapcore whaleviewapi KF6::CoreAddons KF6::I18n Qt6::Widgets)
whale_use_private_libdir(orcinus_treemap "${KDE_INSTALL_FULL_PLUGINDIR}/orcinus/views")
set_target_properties(orcinus_treemap PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${WHALE_VIEW_PLUGIN_BUILD_DIR}/orcinus/views)
