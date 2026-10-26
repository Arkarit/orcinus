# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: BSD-3-Clause

# The development automation (src/whale/devmcp) must stay out of regular builds:
# the option is OFF by default, every hook is guarded, and a binary built without
# the option contains nothing of it.

foreach(var SOURCE_DIR OPTION_VALUE WHALE_BINARY)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/CMakeLists.txt" root)
if(NOT root MATCHES "option\\(WHALE_ENABLE_DEV_MCP \"[^\"]*\" OFF\\)")
    message(FATAL_ERROR "WHALE_ENABLE_DEV_MCP must be an option that is OFF by default")
endif()

file(READ "${SOURCE_DIR}/src/whale/CMakeLists.txt" whale)
if(NOT whale MATCHES "if\\(WHALE_ENABLE_DEV_MCP\\)[ \t\r\n]*add_subdirectory\\(devmcp\\)[ \t\r\n]*endif\\(\\)")
    message(FATAL_ERROR "src/whale/CMakeLists.txt must add devmcp only if(WHALE_ENABLE_DEV_MCP)")
endif()

# Every mention of the automation outside of src/whale/devmcp must be inside "#if WHALE_ENABLE_DEV_MCP".
file(GLOB_RECURSE sources "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
foreach(source IN LISTS sources)
    if(source MATCHES "/src/whale/devmcp/")
        continue()
    endif()
    file(STRINGS "${source}" lines)
    set(guarded FALSE)
    foreach(line IN LISTS lines)
        if(line MATCHES "^#if WHALE_ENABLE_DEV_MCP")
            set(guarded TRUE)
        elseif(line MATCHES "^#endif")
            set(guarded FALSE)
        elseif(line MATCHES "WhaleDevAutomation|whaledevautomation" AND NOT guarded)
            message(FATAL_ERROR "Unguarded development automation in ${source}: ${line}")
        endif()
    endforeach()
endforeach()

if(NOT OPTION_VALUE)
    file(STRINGS "${WHALE_BINARY}" found REGEX "\\.DevAutomation|WHALE_DEV_MCP")
    if(found)
        message(FATAL_ERROR "${WHALE_BINARY} was built without WHALE_ENABLE_DEV_MCP but contains the automation")
    endif()
endif()
