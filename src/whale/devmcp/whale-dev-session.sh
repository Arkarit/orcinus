#!/bin/bash
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

# Starts the Whale of a build tree with the development automation, in a profile of
# its own, so test runs never change the settings of the Whale used every day.
#
#   whale-dev-session.sh [--build DIR] [--profile DIR] [--offscreen] [URL...]
#
# Defaults: --build ../build-whale (relative to the source tree), --profile
# <build>/whale-dev-profile. The look (colors, fonts, icons) is copied from the
# user's kdeglobals. Prints the PID; stop Whale with "kill <pid>".

set -euo pipefail

src_dir=$(cd "$(dirname "$0")/../../.." && pwd)
build_dir="$src_dir/../build-whale"
profile=""
platform=""
urls=()

while [ $# -gt 0 ]; do
    case "$1" in
        --build) build_dir=$2; shift 2 ;;
        --profile) profile=$2; shift 2 ;;
        --offscreen) platform=offscreen; shift ;;
        *) urls+=("$1"); shift ;;
    esac
done

build_dir=$(cd "$build_dir" && pwd)
profile=${profile:-$build_dir/whale-dev-profile}
mkdir -p "$profile"/{config,data,cache,state}
if [ -f "$HOME/.config/kdeglobals" ] && [ ! -f "$profile/config/kdeglobals" ]; then
    cp "$HOME/.config/kdeglobals" "$profile/config/kdeglobals"
fi

# prefix.sh is not written for "set -u".
set +u
# shellcheck disable=SC1091
source "$build_dir/prefix.sh"
set -u
export XDG_CONFIG_HOME=$profile/config XDG_DATA_HOME=$profile/data XDG_CACHE_HOME=$profile/cache XDG_STATE_HOME=$profile/state
export WHALE_DEV_MCP=1
if [ -n "$platform" ]; then
    export QT_QPA_PLATFORM=$platform
fi

"$build_dir/bin/whale" --new-window "${urls[@]}" > "$profile/whale.log" 2>&1 &
echo $!
