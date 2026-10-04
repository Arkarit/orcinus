# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Whale view plugins, see .privatedoc/whale-view-plugins-plan.md.
#
# Nothing here is installed yet: no installed Whale binary uses the plugin API before
# milestone M2. Until then kcoreaddons_add_plugin must not install the example plugin
# (it would load against a libwhaleviewapi that is not installed).
set(KCOREADDONS_INTERNAL_SKIP_PLUGIN_INSTALLATION ON)

# Exact version of the view plugin API. Plugins must be built against the same value.
set(WHALE_VIEW_API_VERSION 1)

# kcoreaddons_add_plugin puts plugins below CMAKE_LIBRARY_OUTPUT_DIRECTORY (bin/), but there
# bin/whale is the executable, so bin/whale/views cannot exist. View plugins of the build
# tree therefore go to plugins/whale/views; add that directory to QT_PLUGIN_PATH.
set(WHALE_VIEW_PLUGIN_BUILD_DIR ${CMAKE_BINARY_DIR}/plugins)

########### public plugin API: libwhaleviewapi ###############

add_library(whaleviewapi SHARED)

target_sources(whaleviewapi PRIVATE
    api/whaleview.cpp
    api/whaleviewcontext.cpp
    api/whaleviewplugin.cpp
)

configure_file(api/whaleviewapiversion.h.in ${CMAKE_CURRENT_BINARY_DIR}/whaleviewapiversion.h @ONLY)
generate_export_header(whaleviewapi BASE_NAME whaleviewapi)

target_include_directories(whaleviewapi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/api
    ${CMAKE_CURRENT_BINARY_DIR}
)

# The API may only depend on Qt and KDE Frameworks, never on Whale/Dolphin internals.
target_link_libraries(whaleviewapi PUBLIC
    Qt6::Widgets
)

set_target_properties(whaleviewapi PROPERTIES
    VERSION ${WHALE_VIEW_API_VERSION}.0.0
    SOVERSION ${WHALE_VIEW_API_VERSION}
)

########### host side, linked into the application ###############

add_library(whaleviewhost STATIC)

target_sources(whaleviewhost PRIVATE
    host/whaleviewregistry.cpp
)

ecm_qt_declare_logging_category(whaleviewhost
    HEADER whaleviewdebug.h
    IDENTIFIER WhaleViewDebug
    CATEGORY_NAME org.kde.whale.views
    DESCRIPTION "Whale view plugins"
    EXPORT DOLPHIN
)

target_include_directories(whaleviewhost PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)

target_link_libraries(whaleviewhost PUBLIC
    whaleviewapi
    KF6::CoreAddons
)

########### plugins ###############

add_subdirectory(plugins/hello)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
