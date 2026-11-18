# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# AI support: the AI panel talks to external agents through the Agent Client Protocol (ACP).
# See .privatedoc/ai-support-plan.md.

########### back channel of the agents (AI6) ###############

set(WHALE_AI_LIBEXEC_DIR "${KDE_INSTALL_LIBEXECDIR}/${WHALE_COMPONENT}")
add_subdirectory(mcp)

########### ACP client core (AI1) and agent settings (AI2), no widgets ###############

add_library(whaleai STATIC)

target_sources(whaleai PRIVATE
    whaleacpconnection.cpp
    whaleacpsession.cpp
    whaleacptypes.cpp
    whaleaiagents.cpp
    whaleaiagenttest.cpp
    whaleaichangelog.cpp
    whaleaicontext.cpp
    whaleailocalmodels.cpp
    whaleailocalruntime.cpp
    whaleaimcpserver.cpp
    whaleaiuiactions.cpp
)

# Where the installed relay is; the build tree's one next to the program comes first.
target_compile_definitions(whaleai PRIVATE WHALE_MCP_INSTALLED="${CMAKE_INSTALL_PREFIX}/${WHALE_AI_LIBEXEC_DIR}/orcinus-mcp")
add_dependencies(whaleai orcinus-mcp)

ecm_qt_declare_logging_category(whaleai
    HEADER whaleaidebug.h
    IDENTIFIER WhaleAiDebug
    CATEGORY_NAME ${WHALE_APP_ID}.ai
    DESCRIPTION "${WHALE_DISPLAY_NAME} AI support"
    EXPORT DOLPHIN
)

target_include_directories(whaleai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Network)
target_link_libraries(whaleai PUBLIC
    Qt6::Core
    Qt6::Network
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
)

########### settings page (AI2) and panel (AI3), part of the application ###############

target_sources(dolphinstatic PRIVATE
    whaleaiagentdialog.cpp
    whaleaicontextchips.cpp
    whaleaidisclaimer.cpp
    whaleaipanel.cpp
    whaleaisettingspage.cpp
    whaleaisettingstools.cpp
    whaleaiprofiletools.cpp
    whaleaitranscript.cpp
    whaleaiwindowtools.cpp
)
target_link_libraries(dolphinstatic whaleai)

########### AI canvas (CV1), part of the application ###############

# See .privatedoc/ai-canvas-plan.md.
if(HAVE_AI_CANVAS)
    target_sources(whaleai PRIVATE
        canvas/whaleaicanvasmodel.cpp
        canvas/whaleaitoolstore.cpp
        canvas/whaleaitoolbackend.cpp
    )
    target_include_directories(whaleai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/canvas)
    target_sources(dolphinstatic PRIVATE
        canvas/whaleaicanvasmedia.cpp
        canvas/whaleaicanvaspanel.cpp
        canvas/whaleaicanvastools.cpp
        canvas/whaleaiquickwidget.cpp
        canvas/whaleaiqmlguard.cpp
        canvas/whaleaitoolapi.cpp
        canvas/whaleaitooltab.cpp
        canvas/whaleaitoolguide.cpp
        canvas/whaleaitooltools.cpp
        canvas/whaleaicanvas.qrc
    )
    target_link_libraries(dolphinstatic Qt6::Qml Qt6::Quick Qt6::QuickWidgets Qt6::OpenGL)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
