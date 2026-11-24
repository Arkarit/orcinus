# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Assembles the Orcinus handbook (.privatedoc/handbook-plan.md), run with cmake -P:
#
#   -DMEINPROC=<meinproc6> -DXSL=<db5-to-db45.xsl> -DPARTS5=<Asciidoctor's DocBook 5 output>
#   -DPARTS=<converted parts, written> -DFRAME=<index.docbook.in> -DDOLPHIN=<doc/index.docbook>
#   -DOUTPUT=<assembled index.docbook, written> -DVERSION=<Orcinus version>
#
# 1. Converts Orcinus' parts from DocBook 5 into KDE's DocBook 4.5 dialect (XSLT via meinproc6).
# 2. Replaces the markers in the frame with text cut out of the parts and of Dolphin's
#    unchanged handbook. The text is copied as it is, so KDE's entities (&dolphin; …) stay
#    references and meinproc6 resolves them as in Dolphin's own handbook. (XInclude would
#    expand them, and the expanded markup is not valid in every place they are used.)
#
# Markers, each a comment of its own in the frame:
#   <!-- orcinus:chapter ID -->                  an Orcinus chapter
#   <!-- orcinus:dolphin-chapter ID -->          a Dolphin chapter
#   <!-- orcinus:dolphin-chapter-body ID -->     a Dolphin chapter without <chapter> and <title>
#   <!-- orcinus:dolphin-chapters [except ID…] --> all Dolphin chapters in their order
#   <!-- orcinus:dolphin-sect1 ID -->            a Dolphin section
#   <!-- orcinus:dolphin-authors -->             the <author>s of Dolphin's handbook
#   <!-- orcinus:dolphin-copyright -->           the <copyright>s of Dolphin's handbook
# @WHALE_VERSION@ and @WHALE_HANDBOOK_DATE@ in the frame are replaced as well.
#
# The cutting relies on chapter, sect1, author and copyright elements not being nested in
# elements of the same name, which DocBook guarantees. An unknown marker or ID is an error;
# a wrong cut shows up in meinproc6 --check, which the build runs on the result.

foreach(var MEINPROC XSL PARTS5 PARTS FRAME DOLPHIN OUTPUT VERSION)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "assemble-handbook.cmake: -D${var}=… is missing")
    endif()
endforeach()

execute_process(
    COMMAND "${MEINPROC}" --stylesheet "${XSL}" --stdout "${PARTS5}"
    OUTPUT_FILE "${PARTS}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Converting ${PARTS5} to DocBook 4.5 failed (${result})")
endif()

file(READ "${PARTS}" parts)
file(READ "${DOLPHIN}" dolphin)
file(READ "${FRAME}" frame)

# All elements <tag …>…</tag> of `content` (whose start tag begins with `start`), as a list of
# "begin:length" pairs, in document order.
function(find_elements content tag start out)
    set(found)
    set(offset 0)
    string(LENGTH "</${tag}>" end_length)
    while(TRUE)
        string(SUBSTRING "${content}" ${offset} -1 rest)
        string(FIND "${rest}" "${start}" begin)
        if(begin EQUAL -1)
            break()
        endif()
        string(SUBSTRING "${rest}" ${begin} -1 element)
        string(FIND "${element}" "</${tag}>" end)
        if(end EQUAL -1)
            message(FATAL_ERROR "assemble-handbook.cmake: no </${tag}> after ${start}")
        endif()
        math(EXPR element_begin "${offset} + ${begin}")
        math(EXPR element_length "${end} + ${end_length}")
        list(APPEND found "${element_begin}:${element_length}")
        math(EXPR offset "${element_begin} + ${element_length}")
    endwhile()
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

function(element_text content range out)
    string(REPLACE ":" ";" range "${range}")
    list(GET range 0 begin)
    list(GET range 1 length)
    string(SUBSTRING "${content}" ${begin} ${length} text)
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# The element <tag id="ID"…>…</tag> of `content`.
function(element_by_id content tag id what out)
    find_elements("${content}" ${tag} "<${tag} id=\"${id}\"" ranges)
    list(LENGTH ranges count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "assemble-handbook.cmake: ${what} has ${count} <${tag} id=\"${id}\">")
    endif()
    element_text("${content}" "${ranges}" text)
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# All elements <tag>…</tag> of `content`, joined by newlines.
function(all_elements content tag start out)
    find_elements("${content}" ${tag} "${start}" ranges)
    set(joined "")
    foreach(range IN LISTS ranges)
        element_text("${content}" "${range}" text)
        string(APPEND joined "${text}\n")
    endforeach()
    set(${out} "${joined}" PARENT_SCOPE)
endfunction()

string(FIND "${dolphin}" "<bookinfo>" bookinfo_begin)
string(FIND "${dolphin}" "</bookinfo>" bookinfo_end)
if(bookinfo_begin EQUAL -1 OR bookinfo_end EQUAL -1)
    message(FATAL_ERROR "assemble-handbook.cmake: no <bookinfo> in ${DOLPHIN}")
endif()
math(EXPR bookinfo_length "${bookinfo_end} - ${bookinfo_begin}")
string(SUBSTRING "${dolphin}" ${bookinfo_begin} ${bookinfo_length} dolphin_bookinfo)

set(result "")
set(rest "${frame}")
while(TRUE)
    string(FIND "${rest}" "<!-- orcinus:" marker_begin)
    if(marker_begin EQUAL -1)
        string(APPEND result "${rest}")
        break()
    endif()
    string(SUBSTRING "${rest}" 0 ${marker_begin} before)
    string(APPEND result "${before}")
    string(SUBSTRING "${rest}" ${marker_begin} -1 rest)
    string(FIND "${rest}" "-->" marker_end)
    math(EXPR after_marker "${marker_end} + 3")
    math(EXPR marker_text_length "${marker_end} - 13")
    string(SUBSTRING "${rest}" 13 ${marker_text_length} marker)
    string(SUBSTRING "${rest}" ${after_marker} -1 rest)

    string(STRIP "${marker}" marker)
    string(REGEX REPLACE "[ \t\r\n]+" ";" words "${marker}")
    list(POP_FRONT words command)

    if(command STREQUAL "chapter")
        element_by_id("${parts}" chapter "${words}" "${PARTS}" text)
    elseif(command STREQUAL "dolphin-chapter")
        element_by_id("${dolphin}" chapter "${words}" "${DOLPHIN}" text)
    elseif(command STREQUAL "dolphin-sect1")
        element_by_id("${dolphin}" sect1 "${words}" "${DOLPHIN}" text)
    elseif(command STREQUAL "dolphin-chapter-body")
        element_by_id("${dolphin}" chapter "${words}" "${DOLPHIN}" text)
        string(FIND "${text}" "</title>" title_end)
        string(FIND "${text}" "</chapter>" chapter_end REVERSE)
        math(EXPR body_begin "${title_end} + 8")
        math(EXPR body_length "${chapter_end} - ${body_begin}")
        string(SUBSTRING "${text}" ${body_begin} ${body_length} text)
    elseif(command STREQUAL "dolphin-chapters")
        set(except "")
        if(words)
            list(POP_FRONT words keyword)
            if(NOT keyword STREQUAL "except")
                message(FATAL_ERROR "assemble-handbook.cmake: dolphin-chapters takes \"except ID…\", not \"${keyword}\"")
            endif()
            set(except "${words}")
        endif()
        find_elements("${dolphin}" chapter "<chapter id=\"" ranges)
        set(text "")
        set(seen "")
        foreach(range IN LISTS ranges)
            element_text("${dolphin}" "${range}" chapter)
            string(REGEX MATCH "^<chapter id=\"([^\"]*)\"" _ "${chapter}")
            list(APPEND seen "${CMAKE_MATCH_1}")
            if(NOT CMAKE_MATCH_1 IN_LIST except)
                string(APPEND text "${chapter}\n\n")
            endif()
        endforeach()
        foreach(id IN LISTS except)
            if(NOT id IN_LIST seen)
                message(FATAL_ERROR "assemble-handbook.cmake: ${DOLPHIN} has no chapter \"${id}\" to leave out")
            endif()
        endforeach()
    elseif(command STREQUAL "dolphin-authors")
        all_elements("${dolphin_bookinfo}" author "<author>" text)
    elseif(command STREQUAL "dolphin-copyright")
        all_elements("${dolphin_bookinfo}" copyright "<copyright>" text)
    else()
        message(FATAL_ERROR "assemble-handbook.cmake: unknown marker \"orcinus:${marker}\" in ${FRAME}")
    endif()
    string(APPEND result "${text}")
endwhile()

# Honours SOURCE_DATE_EPOCH, so reproducible builds stay reproducible.
string(TIMESTAMP date "%Y-%m-%d" UTC)
string(REPLACE "@WHALE_VERSION@" "${VERSION}" result "${result}")
string(REPLACE "@WHALE_HANDBOOK_DATE@" "${date}" result "${result}")

file(WRITE "${OUTPUT}" "${result}")
