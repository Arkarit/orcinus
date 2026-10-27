# SPDX-FileCopyrightText: 2006-2026 KDE Contributors
#
# SPDX-License-Identifier: CC0-1.0

cmake_minimum_required(VERSION 3.16)

# KDE Application Version, managed by release script
set (RELEASE_SERVICE_VERSION_MAJOR "26")
set (RELEASE_SERVICE_VERSION_MINOR "11")
set (RELEASE_SERVICE_VERSION_MICRO "70")
set (RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")
project(Dolphin VERSION ${RELEASE_SERVICE_VERSION})

# Product identity: Orcinus (the internal code name is still Whale). Keep these values
# centralized so the fork-specific changes remain small and easy to review when merging
# Dolphin upstream.
# Orcinus' own version (semantic versioning, release plan RL3). RELEASE_SERVICE_VERSION above
# stays Dolphin's version: the version of the Dolphin code Orcinus is based on, also used for
# the internal libraries; the About dialog shows it as the base.
set(WHALE_VERSION "1.0.0")
set(WHALE_EXECUTABLE "orcinus")
set(WHALE_COMPONENT "orcinus")
set(WHALE_ORG_DOMAIN "arkarit.github.io")
set(WHALE_APP_ID "io.github.arkarit.orcinus")
set(WHALE_DISPLAY_NAME "Orcinus")
set(WHALE_BUG_URL "https://github.com/Arkarit/orcinus/issues")
set(WHALE_HOMEPAGE "https://github.com/Arkarit/orcinus")

option(WHALE_PROVIDE_FILEMANAGER1 "Register org.freedesktop.FileManager1 for Orcinus" OFF)
option(WHALE_INSTALL_DOLPHIN_EXTRAS "Install Dolphin-named compatibility artifacts (may conflict with Dolphin)" OFF)
option(WHALE_DEV_BUILD "Development build: shows \"Orcinus-Dev\" in window titles to tell it apart from the regular Orcinus" OFF)
option(WHALE_ENABLE_DEV_MCP "Development only: UI test automation over D-Bus (started with WHALE_DEV_MCP=1), see src/whale/devmcp" OFF)

set(QT_MIN_VERSION "6.4.0")
set(KF6_MIN_VERSION "6.23.0")

# use C++20 like KF6 itself
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# ECM setup
find_package(ECM ${KF6_MIN_VERSION} CONFIG REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

include(ECMSetupVersion)
include(ECMGenerateHeaders)
include(ECMGenerateDBusServiceFile)
include(ECMConfiguredInstall)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(FeatureSummary)

# Orcinus' private libraries (libdolphinvcs, B2; liborcinusviewapi, B3) are installed into a
# directory of their own. Installed binaries find them through a RUNPATH relative to their own
# location, so packages need no absolute paths.
set(WHALE_PRIVATE_LIBDIR "${KDE_INSTALL_LIBDIR}/${WHALE_COMPONENT}")
function(whale_use_private_libdir target installed_in)
    file(RELATIVE_PATH relative "${installed_in}" "${CMAKE_INSTALL_PREFIX}/${WHALE_PRIVATE_LIBDIR}")
    set_property(TARGET ${target} PROPERTY INSTALL_RPATH "$ORIGIN/${relative}" ${CMAKE_INSTALL_RPATH})
endfunction()
include(ECMQtDeclareLoggingCategory)
include(ECMDeprecationSettings)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckFunctionExists)

ecm_setup_version(${RELEASE_SERVICE_VERSION} VARIABLE_PREFIX DOLPHIN
                  VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/dolphin_version.h"
)

ecm_setup_version(${RELEASE_SERVICE_VERSION} VARIABLE_PREFIX DOLPHINVCS
                  VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/dolphinvcs_version.h"
                  PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/DolphinVcsConfigVersion.cmake"
                  SOVERSION 6
)

ecm_setup_version(${RELEASE_SERVICE_VERSION} VARIABLE_PREFIX DOLPHINPRIVATE
                  SOVERSION 6
)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core
    Concurrent
    Widgets
    Gui
    DBus
)

if (Qt6Gui_VERSION VERSION_GREATER_EQUAL "6.10.0")
    find_package(Qt6GuiPrivate ${QT_MIN_VERSION} REQUIRED NO_MODULE)
endif()

if (UNIX AND NOT APPLE AND NOT HAIKU)
    set(HAVE_X11 TRUE)
endif()

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    KCMUtils
    NewStuff
    CoreAddons
    I18n
    DBusAddons
    Bookmarks
    Config
    KIO
    Parts
    Solid
    IconThemes
    Completion
    TextWidgets
    Notifications
    Crash
    WindowSystem
    WidgetsAddons
    Codecs
    GuiAddons
    ColorScheme
)

# Whale must not submit telemetry under Dolphin's KDE product identity.
set(CMAKE_DISABLE_FIND_PACKAGE_KF6UserFeedback ON)
find_package(KF6UserFeedback ${KF6_MIN_VERSION})
set_package_properties(KF6UserFeedback
        PROPERTIES TYPE OPTIONAL
        PURPOSE "Used for submission of telemetry data"
        )

if(KF6UserFeedback_FOUND)
    set(HAVE_KUSERFEEDBACK TRUE)
endif()

find_package(KF6 ${KF6_MIN_VERSION} OPTIONAL_COMPONENTS
    DocTools
)

find_package(PackageKitQt6)
set_package_properties(PackageKitQt6
        PROPERTIES DESCRIPTION "Software Manager integration"
        TYPE RECOMMENDED
        PURPOSE "Used in the service menu installer"
        )
if(PackageKitQt6_FOUND)
    set(HAVE_PACKAGEKIT TRUE)
endif()

find_package(KF6FileMetaData ${KF6_MIN_VERSION})
set_package_properties(KF6FileMetaData PROPERTIES
                       URL "https://projects.kde.org/kfilemetadata"
                       TYPE REQUIRED
                       PURPOSE "For accessing file metadata labels and filesystem metadata"
                      )

find_package(KF6Baloo ${KF6_MIN_VERSION})
set_package_properties(KF6Baloo PROPERTIES DESCRIPTION "Baloo Core libraries"
                       URL "https://www.kde.org"
                       TYPE OPTIONAL
                       PURPOSE "For adding desktop-wide search and tagging support to dolphin"
                      )

# Whale compatibility: build against the current Kubuntu/KDE release (25.12.3);
# upstream/master already requires ${RELEASE_SERVICE_VERSION} (26.11.70).
find_package(KF6BalooWidgets 25.12.3)
set_package_properties(KF6BalooWidgets PROPERTIES DESCRIPTION "Baloos Widgets"
                       URL "https://www.kde.org"
                       TYPE OPTIONAL
                      )

if (KF6Baloo_FOUND AND KF6BalooWidgets_FOUND)
    message(STATUS "Baloo packages are found")
    set(HAVE_BALOO TRUE)

    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
        Multimedia
        MultimediaWidgets
    )
else ()
    message(WARNING "Baloo packages not found. They are needed for the metadata features of Dolphin (including the information panel).")
endif()

# Videos in Whale's built-in viewer (V3, .privatedoc/design-notes.md).
option(WHALE_WITH_MPV "Play videos in the built-in viewer with libmpv, if it is found" ON)
set(HAVE_MPV FALSE)
if(WHALE_WITH_MPV)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(MPV IMPORTED_TARGET mpv)
    endif()
    find_package(Qt6 ${QT_MIN_VERSION} CONFIG COMPONENTS OpenGLWidgets)
    add_feature_info(libmpv MPV_FOUND "Videos in the built-in viewer")
    if(MPV_FOUND AND Qt6OpenGLWidgets_FOUND)
        set(HAVE_MPV TRUE)
    endif()
endif()

# TODO: drop HAVE_TERMINAL once we are sure the terminal panel works on Windows too.
if(WIN32)
    set(HAVE_TERMINAL FALSE)
else()
    set(HAVE_TERMINAL TRUE)
endif()

# Compatibility with platforms without native fts (e.g. musl)
check_include_files("sys/types.h;sys/stat.h;fts.h" HAVE_FTS_H)
if(HAVE_FTS_H AND NOT HAIKU)
    check_function_exists(fts_open HAVE_FTS_OPEN)
    if(NOT HAVE_FTS_OPEN)
        check_library_exists(fts fts_open "" HAVE_LIB_FTS)
    endif()
endif()

if(HAVE_LIB_FTS)
    set(FTS_LIB fts)
else()
    set(FTS_LIB "")
endif()

add_subdirectory(src)
if(WHALE_INSTALL_DOLPHIN_EXTRAS)
    add_subdirectory(doc)
endif()
add_subdirectory(appiumtests)

# Whale: the tests under src/ run on the offscreen Qt platform, so a plain ctest never opens
# windows on the developer's desktop. Not the Appium tests, which need a real session.
# They also run in English whatever the developer's language: they compare texts.
option(WHALE_TESTS_OFFSCREEN "Run the tests under src/ on the offscreen Qt platform" ON)
function(whale_tests_environment directory)
    get_property(tests DIRECTORY ${directory} PROPERTY TESTS)
    foreach(test IN LISTS tests)
        set_property(TEST ${test} DIRECTORY ${directory} APPEND PROPERTY ENVIRONMENT ${ARGN})
    endforeach()
    get_property(subdirectories DIRECTORY ${directory} PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        whale_tests_environment(${subdirectory} ${ARGN})
    endforeach()
endfunction()
if(BUILD_TESTING)
    set(whale_test_environment "LANGUAGE=en_US")
    if(WHALE_TESTS_OFFSCREEN)
        list(APPEND whale_test_environment "QT_QPA_PLATFORM=offscreen")
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.28)
        message(WARNING "CMake 3.28 or newer is needed to set the test environment; set ${whale_test_environment} yourself")
    else()
        whale_tests_environment(${CMAKE_CURRENT_SOURCE_DIR}/src ${whale_test_environment})
    endif()
endif()

# CMake files
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/DolphinVcs")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/DolphinVcsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/DolphinVcsConfig.cmake"
    INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

if(WHALE_INSTALL_DOLPHIN_EXTRAS)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/DolphinVcsConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/DolphinVcsConfigVersion.cmake"
        DESTINATION "${CMAKECONFIG_INSTALL_DIR}"
        COMPONENT Devel
    )

    install(EXPORT DolphinVcsTargets
        DESTINATION "${CMAKECONFIG_INSTALL_DIR}"
        FILE DolphinVcsTargets.cmake
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/dolphinvcs_version.h"
        DESTINATION "${KDE_INSTALL_INCLUDEDIR}/Dolphin"
        COMPONENT Devel
    )
endif()

if(WHALE_PROVIDE_FILEMANAGER1)
    ecm_generate_dbus_service_file(
        NAME org.freedesktop.FileManager1
        EXECUTABLE "${KDE_INSTALL_FULL_BINDIR}/${WHALE_EXECUTABLE} --daemon"
        SYSTEMD_SERVICE plasma-${WHALE_COMPONENT}.service
        DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
        RENAME ${WHALE_APP_ID}.FileManager1.service
    )

    configure_file(plasma-orcinus.service.in ${CMAKE_CURRENT_BINARY_DIR}/plasma-${WHALE_COMPONENT}.service @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plasma-${WHALE_COMPONENT}.service DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()

ecm_qt_install_logging_categories(
        EXPORT DOLPHIN
        FILE ${WHALE_COMPONENT}.categories
        DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR}
        )

if(WHALE_INSTALL_DOLPHIN_EXTRAS)
    ki18n_install(po)
    if(KF6DocTools_FOUND)
        kdoctools_install(po)
    endif()

    install(FILES completions/dolphin.zsh RENAME _dolphin DESTINATION ${KDE_INSTALL_ZSHAUTOCOMPLETEDIR})
endif()

# Reuse Dolphin's upstream translations under Whale's independent translation
# domain. The source catalogs remain untouched, which keeps upstream updates
# easy to merge and avoids installing files that collide with Dolphin.
find_package(Gettext REQUIRED)
find_program(WHALE_MSGCAT_EXECUTABLE msgcat REQUIRED)
file(GLOB whale_po_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/*/dolphin.po")
set(whale_mo_files)
foreach(whale_po_file IN LISTS whale_po_files)
    get_filename_component(whale_po_dir "${whale_po_file}" DIRECTORY)
    get_filename_component(whale_locale "${whale_po_dir}" NAME)
    set(whale_mo_file "${CMAKE_CURRENT_BINARY_DIR}/locale/${whale_locale}/LC_MESSAGES/${WHALE_COMPONENT}.mo")
    # Whale's own strings (po-orcinus/<locale>/orcinus.po) come first, Dolphin's catalog after them.
    set(whale_own_po_file "${CMAKE_CURRENT_SOURCE_DIR}/po-orcinus/${whale_locale}/orcinus.po")
    if(EXISTS "${whale_own_po_file}")
        set(whale_merged_po_file "${CMAKE_CURRENT_BINARY_DIR}/locale/${whale_locale}/whale-merged.po")
        add_custom_command(
            OUTPUT "${whale_mo_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/${whale_locale}/LC_MESSAGES"
            COMMAND ${WHALE_MSGCAT_EXECUTABLE} --use-first -o "${whale_merged_po_file}" "${whale_own_po_file}" "${whale_po_file}"
            COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -o "${whale_mo_file}" "${whale_merged_po_file}"
            DEPENDS "${whale_po_file}" "${whale_own_po_file}"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT "${whale_mo_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/${whale_locale}/LC_MESSAGES"
            COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -o "${whale_mo_file}" "${whale_po_file}"
            DEPENDS "${whale_po_file}"
            VERBATIM
        )
    endif()
    list(APPEND whale_mo_files "${whale_mo_file}")
    install(FILES "${whale_mo_file}" DESTINATION "${KDE_INSTALL_LOCALEDIR}/${whale_locale}/LC_MESSAGES")
endforeach()
add_custom_target(whale-translations ALL DEPENDS ${whale_mo_files})

set(WHALE_DAEMON_COMPLETION "")
if(WHALE_PROVIDE_FILEMANAGER1)
    set(WHALE_DAEMON_COMPLETION "  '--daemon[Start Orcinus Daemon (only required for DBus Interface).]' \\\n")
endif()
configure_file(completions/orcinus.zsh.in ${CMAKE_CURRENT_BINARY_DIR}/_${WHALE_EXECUTABLE} @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/_${WHALE_EXECUTABLE} DESTINATION ${KDE_INSTALL_ZSHAUTOCOMPLETEDIR})

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(KDEClangFormat)
# add clang-format target
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.cpp *.h *.c)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

include(KDEGitCommitHooks)
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)
