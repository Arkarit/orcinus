# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6Test CONFIG REQUIRED)
include(ECMAddTests)

ecm_add_test(whaledevautomationtest.cpp ${CMAKE_SOURCE_DIR}/src/tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaledevautomationtest
    LINK_LIBRARIES whaledevautomation Qt6::Test)
target_include_directories(whaledevautomationtest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaledevautomationtest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaledevautomationtest whale_hello whale_testlist)

# End to end: a real Whale process, driven only through the MCP adapter.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND NOT WIN32)
    add_test(NAME whale_dev_mcp_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_m3_acceptance.py
            --whale $<TARGET_FILE:dolphin>
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/../adapter/whale_dev_mcp.py
            --plugin-dir ${WHALE_VIEW_PLUGIN_BUILD_DIR}
            --platform offscreen)
    add_test(NAME whale_dev_mcp_m4_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_m4_acceptance.py
            --whale $<TARGET_FILE:dolphin>
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/../adapter/whale_dev_mcp.py
            --plugin-dir ${WHALE_VIEW_PLUGIN_BUILD_DIR}
            --platform offscreen)
    add_test(NAME whale_dev_mcp_m5_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_m5_acceptance.py
            --whale $<TARGET_FILE:dolphin>
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/../adapter/whale_dev_mcp.py
            --plugin-dir ${WHALE_VIEW_PLUGIN_BUILD_DIR}
            --platform offscreen)
    add_test(NAME whale_dev_mcp_m6_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_m6_acceptance.py
            --whale $<TARGET_FILE:dolphin>
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/../adapter/whale_dev_mcp.py
            --plugin-dir ${WHALE_VIEW_PLUGIN_BUILD_DIR}
            --dynamic-plugin-dir ${WHALE_TEST_DYNAMIC_PLUGIN_DIR}
            --platform offscreen)
    add_test(NAME whale_dev_mcp_m7_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_m7_acceptance.py
            --whale $<TARGET_FILE:dolphin>
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/../adapter/whale_dev_mcp.py
            --plugin-dir ${WHALE_VIEW_PLUGIN_BUILD_DIR}
            --platform offscreen)
    add_test(NAME whale_dev_mcp_m9_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_m9_acceptance.py
            --whale $<TARGET_FILE:dolphin>
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/../adapter/whale_dev_mcp.py
            --plugin-dir ${WHALE_VIEW_PLUGIN_BUILD_DIR}
            --platform offscreen)
    add_test(NAME whale_dev_mcp_t1_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_t1_acceptance.py
            --whale $<TARGET_FILE:dolphin>
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/../adapter/whale_dev_mcp.py
            --plugin-dir ${WHALE_VIEW_PLUGIN_BUILD_DIR}
            --platform offscreen)
    add_test(NAME whale_dev_mcp_t2_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_t2_acceptance.py
            --whale $<TARGET_FILE:dolphin>
            --adapter ${CMAKE_CURRENT_SOURCE_DIR}/../adapter/whale_dev_mcp.py
            --plugin-dir ${WHALE_VIEW_PLUGIN_BUILD_DIR}
            --platform offscreen)
    # 77: no D-Bus session bus (or no dbus-run-session) available.
    set_tests_properties(whale_dev_mcp_e2e whale_dev_mcp_m4_e2e whale_dev_mcp_m5_e2e whale_dev_mcp_m6_e2e whale_dev_mcp_m7_e2e
        whale_dev_mcp_m9_e2e whale_dev_mcp_t1_e2e whale_dev_mcp_t2_e2e
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
endif()
