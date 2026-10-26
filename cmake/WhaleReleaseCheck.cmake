# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: BSD-3-Clause
#
# Checks that a configured build directory is fit for a release (release plan RL3): no
# development build, no development automation, no telemetry, an optimized build type, and a
# binary without the automation. Usage:
#   cmake -DBUILD_DIR=../build-whale-system -P cmake/WhaleReleaseCheck.cmake

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

set(problems)
file(STRINGS "${BUILD_DIR}/CMakeCache.txt" cache)
foreach(line IN LISTS cache)
    if(line MATCHES "^(WHALE_DEV_BUILD|WHALE_ENABLE_DEV_MCP):BOOL=(ON|TRUE|1)$")
        list(APPEND problems "${CMAKE_MATCH_1} is on")
    elseif(line MATCHES "^CMAKE_BUILD_TYPE:STRING=(.*)$")
        set(build_type "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT build_type MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    list(APPEND problems "CMAKE_BUILD_TYPE is '${build_type}', not an optimized build type")
endif()

file(READ "${BUILD_DIR}/src/config-dolphin.h" config)
foreach(define HAVE_KUSERFEEDBACK WHALE_DEV_BUILD WHALE_ENABLE_DEV_MCP)
    if(NOT config MATCHES "#define ${define} 0")
        list(APPEND problems "${define} is not 0 in config-dolphin.h")
    endif()
endforeach()

file(GLOB binary "${BUILD_DIR}/bin/*")
foreach(file IN LISTS binary)
    get_filename_component(name "${file}" NAME)
    if(NOT IS_DIRECTORY "${file}" AND NOT name MATCHES "\\.")
        file(STRINGS "${file}" automation REGEX "\\.DevAutomation|WHALE_DEV_MCP" LIMIT_COUNT 1)
        if(automation)
            list(APPEND problems "${name} contains the development automation")
        endif()
    endif()
endforeach()

if(problems)
    list(JOIN problems "\n  " text)
    message(FATAL_ERROR "Not a release configuration (${BUILD_DIR}):\n  ${text}")
endif()
message(STATUS "Release configuration OK: ${BUILD_DIR}")
