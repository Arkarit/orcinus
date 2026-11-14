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

ecm_add_test(whaleaicontexttest.cpp
    TEST_NAME whaleaicontexttest
    LINK_LIBRARIES whaleai Qt6::Test)

ecm_add_test(whaleaisandboxtest.cpp
    TEST_NAME whaleaisandboxtest
    LINK_LIBRARIES whaleai Qt6::Test)
add_dependencies(whaleaisandboxtest orcinus-sandbox)

ecm_add_test(whaleaimcpservertest.cpp
    TEST_NAME whaleaimcpservertest
    LINK_LIBRARIES whaleai Qt6::Test)
target_compile_definitions(whaleaimcpservertest PRIVATE WHALE_MCP_RELAY="$<TARGET_FILE:orcinus-mcp>")
add_dependencies(whaleaimcpservertest orcinus-mcp)
