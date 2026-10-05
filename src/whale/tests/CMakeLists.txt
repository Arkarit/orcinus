# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6Test CONFIG REQUIRED)
include(ECMAddTests)

# A view plugin whose metadata claims a wrong API version. It lives in its own
# plugin directory so that it never shows up when Whale runs from the build tree.
math(EXPR WHALE_VIEW_API_WRONG_VERSION "${WHALE_VIEW_API_VERSION} + 1")
configure_file(whale_test_wrongapi.json.in ${CMAKE_CURRENT_BINARY_DIR}/whale_test_wrongapi.json @ONLY)
set_source_files_properties(whaletestwrongapiplugin.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/whale_test_wrongapi.json)
# A plain module instead of kcoreaddons_add_plugin, because that would install it.
add_library(whale_test_wrongapi MODULE whaletestwrongapiplugin.cpp)
target_link_libraries(whale_test_wrongapi PRIVATE whaleviewapi KF6::CoreAddons)
set(WHALE_TEST_EXTRA_PLUGIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/plugins)
set_target_properties(whale_test_wrongapi PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${WHALE_TEST_EXTRA_PLUGIN_DIR}/whale/views)

ecm_add_test(whaleviewregistrytest.cpp
    TEST_NAME whaleviewregistrytest
    LINK_LIBRARIES whaleviewhost Qt6::Test)
target_compile_definitions(whaleviewregistrytest PRIVATE
    WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}"
    WHALE_TEST_EXTRA_PLUGIN_DIR="${WHALE_TEST_EXTRA_PLUGIN_DIR}")
add_dependencies(whaleviewregistrytest whale_hello whale_test_wrongapi)

ecm_add_test(whaleviewhosttest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleviewhosttest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaleviewhosttest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaleviewhosttest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaleviewhosttest whale_hello whale_testlist)

ecm_add_test(dolphinviewbridgetest.cpp ../../tests/testdir.cpp
    TEST_NAME dolphinviewbridgetest
    LINK_LIBRARIES whaleviewhost Qt6::Test)
target_include_directories(dolphinviewbridgetest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(dolphinviewbridgetest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(dolphinviewbridgetest whale_testlist)

ecm_add_test(whaleselectiontest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleselectiontest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaleselectiontest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaleselectiontest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaleselectiontest whale_testlist)

ecm_add_test(whaleactionstest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleactionstest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaleactionstest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaleactionstest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaleactionstest whale_testlist whale_hello)

# The development automation must stay out of regular builds (see src/whale/devmcp).
add_test(NAME whale_dev_mcp_default_off
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOPTION_VALUE=${WHALE_ENABLE_DEV_MCP}
        -DWHALE_BINARY=$<TARGET_FILE:dolphin>
        -P ${CMAKE_SOURCE_DIR}/cmake/WhaleDevMcpOffTest.cmake)

# Lint: the public plugin API and the plugins must not use Whale/Dolphin internals.
if(NOT WIN32)
add_test(
    NAME whale_view_api_boundary
    COMMAND bash -c
        "result=\$(grep -rnE '#include [<\"](dolphin|kitemviews|views/|settings/|kfileitemmodel|kitemlist|dolphinview)' '${CMAKE_CURRENT_SOURCE_DIR}/../api' '${CMAKE_CURRENT_SOURCE_DIR}/../plugins'); \
         if [ -n \"\$result\" ]; then \
             echo 'The Whale view plugin API and the view plugins must only use Qt, KDE Frameworks and the API itself:'; \
             echo \"\$result\"; \
             exit 1; \
         fi"
)
endif()
