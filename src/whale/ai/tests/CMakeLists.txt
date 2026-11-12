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
