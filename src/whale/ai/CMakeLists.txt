# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# AI support: the AI panel talks to external agents through the Agent Client Protocol (ACP).
# See .privatedoc/ai-support-plan.md.

########### ACP client core (AI1) and agent settings (AI2), no widgets ###############

add_library(whaleai STATIC)

target_sources(whaleai PRIVATE
    whaleacpconnection.cpp
    whaleacpsession.cpp
    whaleacptypes.cpp
    whaleaiagents.cpp
    whaleaiagenttest.cpp
)

ecm_qt_declare_logging_category(whaleai
    HEADER whaleaidebug.h
    IDENTIFIER WhaleAiDebug
    CATEGORY_NAME ${WHALE_APP_ID}.ai
    DESCRIPTION "${WHALE_DISPLAY_NAME} AI support"
    EXPORT DOLPHIN
)

target_include_directories(whaleai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(whaleai PUBLIC
    Qt6::Core
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
)

########### settings page (AI2) and panel (AI3), part of the application ###############

target_sources(dolphinstatic PRIVATE
    whaleaiagentdialog.cpp
    whaleaipanel.cpp
    whaleaisettingspage.cpp
    whaleaitranscript.cpp
)
target_link_libraries(dolphinstatic whaleai)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
