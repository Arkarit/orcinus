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

# Whale product identity. Keep these values centralized so the fork-specific
# changes remain small and easy to review when merging Dolphin upstream.
set(WHALE_EXECUTABLE "whale")
set(WHALE_COMPONENT "whale")
set(WHALE_ORG_DOMAIN "arkarit.github.io")
set(WHALE_APP_ID "io.github.arkarit.whale")
set(WHALE_DISPLAY_NAME "Whale")
set(WHALE_BUG_URL "https://github.com/Arkarit/whale/issues")
set(WHALE_HOMEPAGE "https://github.com/Arkarit/whale")

option(WHALE_PROVIDE_FILEMANAGER1 "Register org.freedesktop.FileManager1 for Whale" OFF)
option(WHALE_INSTALL_DOLPHIN_EXTRAS "Install Dolphin-named compatibility artifacts (may conflict with Dolphin)" OFF)
option(WHALE_DEV_BUILD "Development build: shows \"Whale-Dev\" in window titles to tell it apart from the regular Whale" OFF)

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
        SYSTEMD_SERVICE plasma-whale.service
        DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
        RENAME ${WHALE_APP_ID}.FileManager1.service
    )

    ecm_install_configured_files(INPUT plasma-whale.service.in DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()

ecm_qt_install_logging_categories(
        EXPORT DOLPHIN
        FILE whale.categories
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
file(GLOB whale_po_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/*/dolphin.po")
set(whale_mo_files)
foreach(whale_po_file IN LISTS whale_po_files)
    get_filename_component(whale_po_dir "${whale_po_file}" DIRECTORY)
    get_filename_component(whale_locale "${whale_po_dir}" NAME)
    set(whale_mo_file "${CMAKE_CURRENT_BINARY_DIR}/locale/${whale_locale}/LC_MESSAGES/whale.mo")
    add_custom_command(
        OUTPUT "${whale_mo_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/${whale_locale}/LC_MESSAGES"
        COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -o "${whale_mo_file}" "${whale_po_file}"
        DEPENDS "${whale_po_file}"
        VERBATIM
    )
    list(APPEND whale_mo_files "${whale_mo_file}")
    install(FILES "${whale_mo_file}" DESTINATION "${KDE_INSTALL_LOCALEDIR}/${whale_locale}/LC_MESSAGES")
endforeach()
add_custom_target(whale-translations ALL DEPENDS ${whale_mo_files})

set(WHALE_DAEMON_COMPLETION "")
if(WHALE_PROVIDE_FILEMANAGER1)
    set(WHALE_DAEMON_COMPLETION "  '--daemon[Start Whale Daemon (only required for DBus Interface).]' \\\n")
endif()
configure_file(completions/whale.zsh.in ${CMAKE_CURRENT_BINARY_DIR}/_whale @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/_whale DESTINATION ${KDE_INSTALL_ZSHAUTOCOMPLETEDIR})

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(KDEClangFormat)
# add clang-format target
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.cpp *.h *.c)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

include(KDEGitCommitHooks)
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)
