# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# The Orcinus handbook, help:/orcinus (.privatedoc/handbook-plan.md):
#   orcinus.adoc  → Asciidoctor → DocBook 5
#                 → db5-to-db45.xsl (via meinproc6) → KDE DocBook 4.5
#   index.docbook.in + the above + Dolphin's unchanged ../index.docbook
#                 → assemble-handbook.cmake → book/index.docbook
#                 → meinproc6 --check --cache (fails on invalid markup and unknown IDs)
# Only included when WHALE_HAVE_HANDBOOK is set by the top-level CMakeLists.txt.

# KDocTools names meinproc6 by its imported target; the assembly script needs the path.
if(TARGET ${KDOCTOOLS_MEINPROC_EXECUTABLE})
    set(meinproc "$<TARGET_FILE:${KDOCTOOLS_MEINPROC_EXECUTABLE}>")
else()
    set(meinproc "${KDOCTOOLS_MEINPROC_EXECUTABLE}")
endif()

set(dolphin_handbook ${CMAKE_CURRENT_SOURCE_DIR}/../index.docbook)
set(parts5 ${CMAKE_CURRENT_BINARY_DIR}/orcinus-parts.db5.xml)
set(parts ${CMAKE_CURRENT_BINARY_DIR}/orcinus-parts.docbook)
set(book_dir ${CMAKE_CURRENT_BINARY_DIR}/book)
set(book ${book_dir}/index.docbook)
set(cache ${book_dir}/index.cache.bz2)

file(GLOB adoc_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.adoc)
file(GLOB dolphin_images CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../*.png)
file(GLOB orcinus_images CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.png)

# Both sets of images are installed into the same folder.
foreach(image IN LISTS orcinus_images)
    get_filename_component(name ${image} NAME)
    if(NOT name MATCHES "^orcinus-")
        message(FATAL_ERROR "Handbook image ${name}: Orcinus' images need the prefix orcinus- (no clashes with Dolphin's)")
    endif()
endforeach()

add_custom_command(OUTPUT ${parts5}
    COMMAND ${WHALE_ASCIIDOCTOR_EXECUTABLE} --backend docbook5 --failure-level WARN
            --out-file ${parts5} ${CMAKE_CURRENT_SOURCE_DIR}/orcinus.adoc
    DEPENDS ${adoc_files}
    COMMENT "Converting the Orcinus handbook parts to DocBook"
    VERBATIM)

add_custom_command(OUTPUT ${book} ${parts}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${book_dir}
    COMMAND ${CMAKE_COMMAND}
            -DMEINPROC=${meinproc}
            -DXSL=${CMAKE_CURRENT_SOURCE_DIR}/db5-to-db45.xsl
            -DPARTS5=${parts5}
            -DPARTS=${parts}
            -DFRAME=${CMAKE_CURRENT_SOURCE_DIR}/index.docbook.in
            -DDOLPHIN=${dolphin_handbook}
            -DOUTPUT=${book}
            -DVERSION=${WHALE_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/assemble-handbook.cmake
    DEPENDS ${parts5}
            ${CMAKE_CURRENT_SOURCE_DIR}/db5-to-db45.xsl
            ${CMAKE_CURRENT_SOURCE_DIR}/index.docbook.in
            ${CMAKE_CURRENT_SOURCE_DIR}/assemble-handbook.cmake
            ${dolphin_handbook}
    COMMENT "Assembling the Orcinus handbook"
    VERBATIM)

add_custom_command(OUTPUT ${cache}
    COMMAND ${KDOCTOOLS_MEINPROC_EXECUTABLE} --check --cache ${cache} ${book}
    DEPENDS ${book} ${KDOCTOOLS_CUSTOMIZATION_DIR}/kde-chunk.xsl
    WORKING_DIRECTORY ${book_dir}
    COMMENT "Checking the Orcinus handbook"
    VERBATIM)

add_custom_target(orcinus-handbook ALL DEPENDS ${cache})

# The handbook as HTML pages in <build>/doc/orcinus/html, readable in a web browser (KDE's
# style sheets copied next to them, see browser-html.cmake); later the base for the website
# (HB3). Not part of "all". KHelpCenter does not use these pages, it reads the cache.
set(html_dir ${CMAKE_CURRENT_BINARY_DIR}/html)
find_path(WHALE_KDOCTOOLS_COMMON_DIR kde-default.css
    PATH_SUFFIXES ${KDE_INSTALL_DOCBUNDLEDIR}/en/kdoctools6-common share/doc/HTML/en/kdoctools6-common
    DOC "KDocTools' kdoctools6-common folder (style sheets of the handbook pages)")
add_custom_target(orcinus-handbook-html
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${html_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${html_dir}
    COMMAND ${CMAKE_COMMAND} -E chdir ${html_dir} ${meinproc} --check ${book}
    COMMAND ${CMAKE_COMMAND} -E copy ${dolphin_images} ${orcinus_images} ${html_dir}
    COMMAND ${CMAKE_COMMAND} -DHTML_DIR=${html_dir} -DCOMMON_DIR=${WHALE_KDOCTOOLS_COMMON_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/browser-html.cmake
    DEPENDS ${cache} ${CMAKE_CURRENT_SOURCE_DIR}/browser-html.cmake
    COMMENT "Writing the Orcinus handbook as HTML to ${html_dir}"
    VERBATIM)

install(FILES ${cache} ${book} ${dolphin_images} ${orcinus_images}
        DESTINATION ${KDE_INSTALL_DOCBUNDLEDIR}/en/${WHALE_COMPONENT})
