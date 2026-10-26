# SPDX-FileCopyrightText: 2026 Whale contributors
# SPDX-License-Identifier: BSD-3-Clause

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB_RECURSE whale_source_files LIST_DIRECTORIES FALSE
    "${SOURCE_DIR}/*.cpp"
    "${SOURCE_DIR}/*.h"
    "${SOURCE_DIR}/*.kcfg"
    "${SOURCE_DIR}/*.py"
    "${SOURCE_DIR}/*.qrc"
    "${SOURCE_DIR}/*.qml"
    "${SOURCE_DIR}/*.rc"
    "${SOURCE_DIR}/*.ui"
)

set(forbidden_runtime_identities
    "org\\.kde\\.dolphin-"
    "/dolphin/Dolphin_1"
    "dolphin --new-window"
    "kcfgfile name=\"dolphinrc\""
    # The product was renamed from Whale to Orcinus; only internal code names may still say whale.
    "io\\.github\\.arkarit\\.whale"
    "[\"/]whale[a-z]*rc\""
    "/kxmlgui5/whale"
    "/whale/DevAutomation"
)

foreach(source_file IN LISTS whale_source_files)
    file(READ "${source_file}" source_contents)
    foreach(forbidden_identity IN LISTS forbidden_runtime_identities)
        if(source_contents MATCHES "${forbidden_identity}")
            message(FATAL_ERROR
                "Whale runtime identity regression in ${source_file}: ${forbidden_identity}"
            )
        endif()
    endforeach()
endforeach()
