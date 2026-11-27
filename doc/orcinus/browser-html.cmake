# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Makes the handbook's HTML pages readable in a web browser, run with cmake -P:
#
#   -DHTML_DIR=<the pages written by meinproc6> -DCOMMON_DIR=<KDocTools' kdoctools6-common>
#
# KDE's pages load their style sheets, images and license pages from
# help:/kdoctools6-common/, which only KHelpCenter resolves. This copies that folder into
# HTML_DIR (as real files, also where a distribution installs symlinks) and makes the links
# relative. Without COMMON_DIR the pages stay as they are and look unstyled in a browser.

if(NOT DEFINED HTML_DIR)
    message(FATAL_ERROR "browser-html.cmake: -DHTML_DIR=… is missing")
endif()

if(NOT COMMON_DIR)
    message(WARNING "KDocTools' kdoctools6-common folder was not found: the handbook pages in ${HTML_DIR} have no style sheets in a browser")
    return()
endif()

set(common_out "${HTML_DIR}/kdoctools6-common")
file(MAKE_DIRECTORY "${common_out}")
file(GLOB common_files RELATIVE "${COMMON_DIR}" "${COMMON_DIR}/*")
foreach(name IN LISTS common_files)
    if(NOT IS_DIRECTORY "${COMMON_DIR}/${name}")
        configure_file("${COMMON_DIR}/${name}" "${common_out}/${name}" COPYONLY)
    endif()
endforeach()

function(make_links_relative dir prefix)
    file(GLOB pages "${dir}/*.html")
    foreach(page IN LISTS pages)
        file(READ "${page}" text)
        string(REPLACE "help:/kdoctools6-common/" "${prefix}" text "${text}")
        file(WRITE "${page}" "${text}")
    endforeach()
endfunction()

make_links_relative("${HTML_DIR}" "kdoctools6-common/")
make_links_relative("${common_out}" "")
