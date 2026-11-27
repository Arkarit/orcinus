#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Developer helper: builds the Orcinus handbook as HTML pages in the development build
# ../build-whale (next to the checkout) and opens them in the default web browser.
# The pages are written to ../build-whale/doc/orcinus/html (see doc/orcinus/CMakeLists.txt).

set -e

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/../build-whale"
index="${build_dir}/doc/orcinus/html/index.html"

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
    echo "The development build is not configured yet. Expected ${build_dir}." >&2
    exit 1
fi

if ! grep -q '^WHALE_ASCIIDOCTOR_EXECUTABLE:FILEPATH=/' "${build_dir}/CMakeCache.txt"; then
    echo "The handbook is not part of this build (asciidoctor or KF6DocTools missing, or WHALE_BUILD_HANDBOOK=OFF)." >&2
    exit 1
fi

cmake --build "${build_dir}" --target orcinus-handbook-html

xdg-open "${index}" >/dev/null 2>&1 &
echo "Opened ${index}"
