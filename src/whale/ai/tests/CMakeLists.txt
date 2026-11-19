# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6Test CONFIG REQUIRED)
include(ECMAddTests)

# A scripted ACP agent, only for the tests; never installed.
add_executable(whaleacpfakeagent whaleacpfakeagent.cpp)
target_link_libraries(whaleacpfakeagent PRIVATE Qt6::Core)

ecm_add_test(whaleacpsessiontest.cpp
    TEST_NAME whaleacpsessiontest
    LINK_LIBRARIES whaleai Qt6::Test)
target_compile_definitions(whaleacpsessiontest PRIVATE WHALE_ACP_FAKE_AGENT="$<TARGET_FILE:whaleacpfakeagent>")
add_dependencies(whaleacpsessiontest whaleacpfakeagent)

ecm_add_test(whaleaichangelogtest.cpp
    TEST_NAME whaleaichangelogtest
    LINK_LIBRARIES whaleai Qt6::Test)

ecm_add_test(whaleaiconversationstoretest.cpp
    TEST_NAME whaleaiconversationstoretest
    LINK_LIBRARIES whaleai Qt6::Test)

ecm_add_test(whaleaiagentstest.cpp
    TEST_NAME whaleaiagentstest
    LINK_LIBRARIES whaleai Qt6::Test)
target_compile_definitions(whaleaiagentstest PRIVATE WHALE_ACP_FAKE_AGENT="$<TARGET_FILE:whaleacpfakeagent>")
add_dependencies(whaleaiagentstest whaleacpfakeagent)

ecm_add_test(whaleaisettingspagetest.cpp ${CMAKE_SOURCE_DIR}/src/tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleaisettingspagetest
    LINK_LIBRARIES dolphinstatic Qt6::Test)
target_include_directories(whaleaisettingspagetest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(whaleaisettingspagetest PRIVATE WHALE_ACP_FAKE_AGENT="$<TARGET_FILE:whaleacpfakeagent>")
add_dependencies(whaleaisettingspagetest whaleacpfakeagent)

ecm_add_test(whaleaipaneltest.cpp ${CMAKE_SOURCE_DIR}/src/tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleaipaneltest
    LINK_LIBRARIES dolphinstatic Qt6::Test)
target_include_directories(whaleaipaneltest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(whaleaipaneltest PRIVATE WHALE_ACP_FAKE_AGENT="$<TARGET_FILE:whaleacpfakeagent>")
add_dependencies(whaleaipaneltest whaleacpfakeagent)

ecm_add_test(whaleailocalmodelstest.cpp
    TEST_NAME whaleailocalmodelstest
    LINK_LIBRARIES whaleai Qt6::Test)

ecm_add_test(whaleailocalruntimetest.cpp
    TEST_NAME whaleailocalruntimetest
    LINK_LIBRARIES whaleai Qt6::Test)

ecm_add_test(whaleailocalstratatest.cpp
    TEST_NAME whaleailocalstratatest
    LINK_LIBRARIES whaleai Qt6::Test)

ecm_add_test(whaleaicontexttest.cpp
    TEST_NAME whaleaicontexttest
    LINK_LIBRARIES whaleai Qt6::Test)

ecm_add_test(whaleaimcpservertest.cpp
    TEST_NAME whaleaimcpservertest
    LINK_LIBRARIES whaleai Qt6::Test)
target_compile_definitions(whaleaimcpservertest PRIVATE WHALE_MCP_RELAY="$<TARGET_FILE:orcinus-mcp>")
add_dependencies(whaleaimcpservertest orcinus-mcp)

# The AI canvas (CV1).
if(HAVE_AI_CANVAS)
    ecm_add_test(whaleaicanvasmodeltest.cpp
        TEST_NAME whaleaicanvasmodeltest
        LINK_LIBRARIES whaleai Qt6::Gui Qt6::Test)

    ecm_add_test(whaleaicanvaspaneltest.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
        TEST_NAME whaleaicanvaspaneltest
        LINK_LIBRARIES dolphinstatic Qt6::Test)
    target_include_directories(whaleaicanvaspaneltest PRIVATE ${CMAKE_SOURCE_DIR}/src)

    # The backends of canvas tools (CV4), with a fake backend in Python (skipped without python3).
    ecm_add_test(whaleaitoolbackendtest.cpp
        TEST_NAME whaleaitoolbackendtest
        LINK_LIBRARIES whaleai Qt6::Test)

    # The tools of the canvas (CV2), with the example tool in data/tools.
    ecm_add_test(whaleaitooltest.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
        TEST_NAME whaleaitooltest
        LINK_LIBRARIES dolphinstatic Qt6::Test)
    target_include_directories(whaleaitooltest PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(whaleaitooltest PRIVATE WHALE_AI_TEST_TOOLS="${CMAKE_CURRENT_SOURCE_DIR}/data/tools")
endif()
