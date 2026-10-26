#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Developer helper: starts the development build in ../build-whale (next to the checkout) with
# its private install prefix, in a new window.

set -e

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/../build-whale"

if [[ ! -f "${build_dir}/prefix.sh" || ! -x "${build_dir}/bin/orcinus" ]]; then
    echo "Orcinus is not built yet. Expected ${build_dir}/bin/orcinus." >&2
    exit 1
fi

# Load the private install prefix so Orcinus finds its current icons and other resources.
source "${build_dir}/prefix.sh"

exec "${build_dir}/bin/orcinus" --new-window "$@"
