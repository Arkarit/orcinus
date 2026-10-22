# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6Test CONFIG REQUIRED)
include(ECMAddTests)

# The test views Hello and Test List: test fixtures in a plugin directory of their own, which
# only the tests (and the dev MCP acceptance tests) add to the plugin path. Whale started from
# the build tree never finds them, and they are never installed.
set(WHALE_TEST_VIEW_PLUGIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/testviewplugins)
set(WHALE_TEST_VIEW_PLUGIN_DIR ${WHALE_TEST_VIEW_PLUGIN_DIR} PARENT_SCOPE)
add_subdirectory(plugins/hello)
add_subdirectory(plugins/testlist)
add_compile_definitions(WHALE_TEST_VIEW_PLUGIN_DIR="${WHALE_TEST_VIEW_PLUGIN_DIR}")

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

# A view whose capabilities change with the location and its state (M6), in a plugin
# directory of its own that only the tests use.
configure_file(whale_test_dynamic.json.in ${CMAKE_CURRENT_BINARY_DIR}/whale_test_dynamic.json @ONLY)
set_source_files_properties(whaletestdynamicplugin.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/whale_test_dynamic.json)
add_library(whale_test_dynamic MODULE whaletestdynamicplugin.cpp)
target_link_libraries(whale_test_dynamic PRIVATE whaleviewapi KF6::CoreAddons Qt6::Widgets)
set(WHALE_TEST_DYNAMIC_PLUGIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/dynamicplugins)
set(WHALE_TEST_DYNAMIC_PLUGIN_DIR ${WHALE_TEST_DYNAMIC_PLUGIN_DIR} PARENT_SCOPE)
set_target_properties(whale_test_dynamic PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${WHALE_TEST_DYNAMIC_PLUGIN_DIR}/whale/views)

ecm_add_test(whaleviewregistrytest.cpp
    TEST_NAME whaleviewregistrytest
    LINK_LIBRARIES whaleviewhost Qt6::Test)
target_compile_definitions(whaleviewregistrytest PRIVATE
    WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}"
    WHALE_TEST_EXTRA_PLUGIN_DIR="${WHALE_TEST_EXTRA_PLUGIN_DIR}")
add_dependencies(whaleviewregistrytest whale_hello whale_test_wrongapi whale_treemap)

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

ecm_add_test(whaleviewservicestest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleviewservicestest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaleviewservicestest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaleviewservicestest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaleviewservicestest whale_treemap)

ecm_add_test(whalefileviewtoolbartest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalefileviewtoolbartest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whalefileviewtoolbartest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whalefileviewtoolbartest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whalefileviewtoolbartest whale_treemap)

ecm_add_test(whaletoolbarconfigtest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaletoolbarconfigtest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaletoolbarconfigtest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaletoolbarconfigtest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")

ecm_add_test(whaletoolbarmigrationtest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaletoolbarmigrationtest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaletoolbarmigrationtest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaletoolbarmigrationtest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")

ecm_add_test(whaleactionstest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleactionstest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaleactionstest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaleactionstest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaleactionstest whale_testlist whale_hello)

ecm_add_test(whalepersistencetest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalepersistencetest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whalepersistencetest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whalepersistencetest PRIVATE
    WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}"
    WHALE_TEST_DYNAMIC_PLUGIN_DIR="${WHALE_TEST_DYNAMIC_PLUGIN_DIR}")
add_dependencies(whalepersistencetest whale_testlist whale_hello whale_test_dynamic)

ecm_add_test(whalepreviewtest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalepreviewtest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whalepreviewtest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whalepreviewtest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whalepreviewtest whale_testlist whale_hello)

ecm_add_test(whaledragdroptest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaledragdroptest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaledragdroptest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaledragdroptest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaledragdroptest whale_testlist)

ecm_add_test(whaletreescannertest.cpp
    TEST_NAME whaletreescannertest
    LINK_LIBRARIES whaleviewhost Qt6::Test)

ecm_add_test(whaletreecachetest.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaletreecachetest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaletreecachetest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaletreecachetest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaletreecachetest whale_testlist)

# M9: the treemap's layout and renderer (deterministic, proportional, no overlaps).
ecm_add_test(whaletreemaplayouttest.cpp
    TEST_NAME whaletreemaplayouttest
    LINK_LIBRARIES whaletreemapcore Qt6::Test)

# M9: the treemap view in a real main window (scan, cache, hover, selection, navigation, state).
ecm_add_test(whaletreemaptest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaletreemaptest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaletreemaptest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaletreemaptest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaletreemaptest whale_treemap whale_testlist)

# T1: operations on deep entries (WhaleFileOperations) and the treemap's context menu.
ecm_add_test(whaledeepoperationstest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaledeepoperationstest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaledeepoperationstest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaledeepoperationstest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaledeepoperationstest whale_treemap)

# T2: Whale's actions on a plugin view's own selection (WhaleCommandRouter), shortcuts, split view.
ecm_add_test(whalecommandroutingtest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalecommandroutingtest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whalecommandroutingtest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whalecommandroutingtest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whalecommandroutingtest whale_treemap whale_testlist)

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
        "result=\$(grep -rnE '#include [<\"](dolphin|kitemviews|views/|settings/|kfileitemmodel|kitemlist|dolphinview)' '${CMAKE_CURRENT_SOURCE_DIR}/../api' '${CMAKE_CURRENT_SOURCE_DIR}/../plugins' '${CMAKE_CURRENT_SOURCE_DIR}/plugins'); \
         if [ -n \"\$result\" ]; then \
             echo 'The Whale view plugin API and the view plugins must only use Qt, KDE Frameworks and the API itself:'; \
             echo \"\$result\"; \
             exit 1; \
         fi"
)
# Lint: a view area must not look up an action set of its window; there may be several
# (the window's, and one per view area). Window-wide things come from WhaleViewServices.
add_test(
    NAME whale_view_host_no_action_lookup
    COMMAND bash -c
        "result=\$(grep -rnE 'findChild(ren)?<WhaleViewActions' '${CMAKE_CURRENT_SOURCE_DIR}/../host'); \
         if [ -n \"\$result\" ]; then \
             echo 'The view host must use WhaleViewServices, not look up WhaleViewActions:'; \
             echo \"\$result\"; \
             exit 1; \
         fi"
)
# Lint: the toolbars must not depend on there being two views (a left/right or primary/
# secondary one); a file view is one instance among any number, also in a dock later.
add_test(
    NAME whale_toolbars_no_pane_sides
    COMMAND bash -c
        "result=\$(grep -rniwE 'primary[a-z]*|secondary[a-z]*|inactiveViewContainer|left|right' '${CMAKE_CURRENT_SOURCE_DIR}/../toolbars'); \
         if [ -n \"\$result\" ]; then \
             echo 'The toolbar code must not refer to the sides of a split view:'; \
             echo \"\$result\"; \
             exit 1; \
         fi"
)
# ... and the built plugins must not link Whale's or Dolphin's private libraries.
add_test(
    NAME whale_view_plugin_dependencies
    COMMAND bash -c
        "status=0; \
         for plugin in '${WHALE_VIEW_PLUGIN_BUILD_DIR}'/whale/views/*.so '${WHALE_TEST_VIEW_PLUGIN_DIR}'/whale/views/*.so; do \
             needed=\$(readelf -d \"\$plugin\" | grep NEEDED); \
             if echo \"\$needed\" | grep -qiE 'whaleprivate|dolphin|whaleviewhost'; then \
                 echo \"\$plugin links private libraries:\"; echo \"\$needed\"; status=1; \
             fi; \
             echo \"\$needed\" | grep -q whaleviewapi || { echo \"\$plugin does not use libwhaleviewapi\"; status=1; }; \
         done; \
         ls '${WHALE_VIEW_PLUGIN_BUILD_DIR}'/whale/views/whale_treemap.so >/dev/null || status=1; \
         for test_view in whale_hello whale_testlist; do \
             if [ -e '${WHALE_VIEW_PLUGIN_BUILD_DIR}'/whale/views/\$test_view.so ]; then \
                 echo \"The test view \$test_view must not be among the product's view plugins\"; status=1; \
             fi; \
         done; \
         exit \$status"
)
endif()

# O3: the preferred view of a location.
ecm_add_test(whaleviewpreferencestest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleviewpreferencestest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaleviewpreferencestest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaleviewpreferencestest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whaleviewpreferencestest whale_treemap whale_testlist)

# F1: "Show flat (no folders)", the file list showing the files below its location.
ecm_add_test(whalerecursivecontenttest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalerecursivecontenttest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whalerecursivecontenttest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whalerecursivecontenttest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whalerecursivecontenttest whale_treemap)
set_tests_properties(whalerecursivecontenttest PROPERTIES TIMEOUT 600)

# The View menus: one level of view modes (a common submenu for more than five), the view
# options enabled by what the shown view supports.
ecm_add_test(whaleviewmenutest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whaleviewmenutest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whaleviewmenutest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whaleviewmenutest PRIVATE
    WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}"
    WHALE_TEST_DYNAMIC_PLUGIN_DIR="${WHALE_TEST_DYNAMIC_PLUGIN_DIR}")
add_dependencies(whaleviewmenutest whale_treemap whale_testlist whale_hello whale_test_dynamic)

ecm_add_test(whalelocationbartest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalelocationbartest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whalelocationbartest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whalelocationbartest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whalelocationbartest whale_treemap)

# Expandable folders in the compact view, expanding a whole branch (CX1, CX2).
ecm_add_test(whalecompactexpansiontest.cpp ../../tests/testdir.cpp
    TEST_NAME whalecompactexpansiontest
    LINK_LIBRARIES dolphinprivate Qt6::Test)
target_include_directories(whalecompactexpansiontest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests ${CMAKE_SOURCE_DIR}/src)

# Always following symbolic links to their target (Folders & Tabs setting).
ecm_add_test(whalesymlinktargettest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalesymlinktargettest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whalesymlinktargettest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)

# The lock of a split view (LK).
ecm_add_test(whalesplitlocktest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalesplitlocktest
    LINK_LIBRARIES whaleviewhost dolphinstatic Qt6::Test)
target_include_directories(whalesplitlocktest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests)
target_compile_definitions(whalesplitlocktest PRIVATE WHALE_TEST_PLUGIN_DIR="${WHALE_VIEW_PLUGIN_BUILD_DIR}")
add_dependencies(whalesplitlocktest whale_treemap whale_testlist)

# The selection limited to one folder while the views of a split view are linked (LK3).
ecm_add_test(whaleselectionlimittest.cpp ../../tests/testdir.cpp
    TEST_NAME whaleselectionlimittest
    LINK_LIBRARIES dolphinprivate Qt6::Test)
target_include_directories(whaleselectionlimittest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests ${CMAKE_SOURCE_DIR}/src)

# The model, registry and file of styles (ST1).
ecm_add_test(whalestylestoretest.cpp
    TEST_NAME whalestylestoretest
    LINK_LIBRARIES dolphinstatic Qt6::Test)

# The element types of styles at a main window (ST2).
ecm_add_test(whalestyleelementstest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalestyleelementstest
    LINK_LIBRARIES dolphinstatic Qt6::Test)
target_include_directories(whalestyleelementstest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests ${CMAKE_SOURCE_DIR}/src)

# Applying, capturing and the actions of styles in main windows (ST3).
ecm_add_test(whalestylemanagertest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalestylemanagertest
    LINK_LIBRARIES dolphinstatic Qt6::Test)
target_include_directories(whalestylemanagertest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests ${CMAKE_SOURCE_DIR}/src)

# The page "Styles" of the settings dialog (ST4).
ecm_add_test(whalestylessettingspagetest.cpp ../../tests/testdir.cpp ${CMAKE_SOURCE_DIR}/src/dolphin.qrc
    TEST_NAME whalestylessettingspagetest
    LINK_LIBRARIES dolphinstatic Qt6::Test)
target_include_directories(whalestylessettingspagetest PRIVATE ${CMAKE_SOURCE_DIR}/src/tests ${CMAKE_SOURCE_DIR}/src)
