# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# AI support: the AI panel talks to external agents through the Agent Client Protocol (ACP).
# See .privatedoc/ai-support-plan.md.

########### sandbox for the agents (AI5d) ###############

set(WHALE_SANDBOX_DIR "${KDE_INSTALL_LIBEXECDIR}/${WHALE_COMPONENT}")
set(WHALE_APPARMOR_DIR "/etc/apparmor.d" CACHE PATH "Where the AppArmor profile of orcinus-sandbox goes (WHALE_INSTALL_APPARMOR_PROFILE)")
add_subdirectory(sandbox)

########### ACP client core (AI1) and agent settings (AI2), no widgets ###############

add_library(whaleai STATIC)

target_sources(whaleai PRIVATE
    whaleacpconnection.cpp
    whaleacpsession.cpp
    whaleacptypes.cpp
    whaleaiagents.cpp
    whaleaiagenttest.cpp
    whaleaicontext.cpp
    whaleaisandbox.cpp
)

# Where the installed sandbox helper is; the build tree's one next to the program comes first.
target_compile_definitions(whaleai PRIVATE WHALE_SANDBOX_INSTALLED="${CMAKE_INSTALL_PREFIX}/${WHALE_SANDBOX_DIR}/orcinus-sandbox")
add_dependencies(whaleai orcinus-sandbox)

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
    whaleaicontextchips.cpp
    whaleaipanel.cpp
    whaleaisettingspage.cpp
    whaleaitranscript.cpp
)
target_link_libraries(dolphinstatic whaleai)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
