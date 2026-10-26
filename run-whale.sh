#!/usr/bin/env bash

# SPDX-License-Identifier: GPL-2.0-or-later

set -e

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/../build-whale"

if [[ ! -f "${build_dir}/prefix.sh" || ! -x "${build_dir}/bin/orcinus" ]]; then
    echo "Whale is not built yet. Expected ${build_dir}/bin/orcinus." >&2
    exit 1
fi

# Load the private install prefix so Whale finds its current icons and other resources.
source "${build_dir}/prefix.sh"

exec "${build_dir}/bin/orcinus" --new-window "$@"
