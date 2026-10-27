#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Builds the distribution test containers and builds and tests Orcinus in them (release plan
# RL4). Needs Podman; everything runs rootless and changes nothing outside the images and the
# build directories. See README.md in this directory.
#
#   containers.sh list                  distributions and their images
#   containers.sh images [DISTRO...]    build kde-dev:DISTRO and orcinus-dev:DISTRO
#   containers.sh test [DISTRO...]      build this checkout and run its tests in orcinus-dev:DISTRO
#   containers.sh shell DISTRO          interactive shell in orcinus-dev:DISTRO (copy of the tree in /src)
#
# Without DISTRO, images and test cover all distributions. Extra arguments after "--" go to
# ctest (test only). Build directories: $ORCINUS_CONTAINER_BUILD_ROOT/DISTRO, by default
# ../build-containers next to the checkout.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "${here}/../.." && pwd)"
build_root="${ORCINUS_CONTAINER_BUILD_ROOT:-$(dirname "${source_dir}")/build-containers}"

# name | package manager family | base image | extra build arguments of the kde-dev image
distros=(
    "ubuntu-26.04|apt|docker.io/library/ubuntu:26.04|"
    "ubuntu-26.10|apt|docker.io/library/ubuntu:26.10|"
    "debian-testing|apt|docker.io/library/debian:testing|"
    "neon|apt|docker.io/library/ubuntu:24.04|--build-arg=NEON=1"
    "fedora-45|dnf|registry.fedoraproject.org/fedora:45|"
    "opensuse-tumbleweed|zypper|registry.opensuse.org/opensuse/tumbleweed:latest|"
    "arch|pacman|docker.io/library/archlinux:latest|"
)

die() {
    echo "containers.sh: $*" >&2
    exit 1
}

lookup() {
    local entry
    for entry in "${distros[@]}"; do
        if [ "${entry%%|*}" = "$1" ]; then
            echo "${entry}"
            return
        fi
    done
    die "unknown distribution '$1' (see: containers.sh list)"
}

all_names() {
    local entry
    for entry in "${distros[@]}"; do
        echo "${entry%%|*}"
    done
}

build_images() {
    local name family base extra
    IFS='|' read -r name family base extra <<< "$(lookup "$1")"
    echo "=== ${name}: kde-dev"
    # shellcheck disable=SC2086 # extra holds zero or one argument
    podman build --pull=newer -t "localhost/kde-dev:${name}" --build-arg "BASE_IMAGE=${base}" ${extra} \
        -f "${here}/kde-dev/${family}.Containerfile" "${here}"
    echo "=== ${name}: orcinus-dev"
    podman build -t "localhost/orcinus-dev:${name}" --build-arg "KDE_DEV_IMAGE=localhost/kde-dev:${name}" \
        -f "${here}/orcinus/${family}.Containerfile" "${here}"
}

# Runs as the calling user (keep-id), so the build directory belongs to them and the tests do
# not run as root. HOME is a fresh directory in the container.
#
# The container gets a copy of the working tree without .git (rsync keeps it incremental): the
# configuration writes into the source directory (.clang-format, Git hooks), which must not
# change the real checkout.
run_in_container() {
    local name="$1"
    shift
    mkdir -p "${build_root}/${name}/src" "${build_root}/${name}/build"
    rsync -a --delete --exclude=/.git --exclude='/build*/' "${source_dir}/" "${build_root}/${name}/src/"
    podman run --rm "${tty_args[@]}" --userns=keep-id --user "$(id -u):$(id -g)" \
        -v "${build_root}/${name}/src:/src" -v "${build_root}/${name}/build:/build" -w /build \
        -e HOME=/tmp/home -e XDG_RUNTIME_DIR=/tmp/runtime \
        -e QT_QPA_PLATFORM=offscreen -e QT_STYLE_OVERRIDE=breeze \
        "localhost/orcinus-dev:${name}" \
        bash -c 'mkdir -m 700 "${HOME}" "${XDG_RUNTIME_DIR}" && exec "$@"' bash "$@"
}

run_tests() {
    local name="$1"
    shift
    lookup "${name}" > /dev/null
    echo "=== ${name}: build and test"
    tty_args=()
    # shellcheck disable=SC2016 # expanded in the container
    run_in_container "${name}" bash -c '
        set -e
        cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON > configure.log 2>&1 \
            || { cat configure.log; exit 1; }
        grep -E "^-- (The following|  \*)" configure.log || true
        cmake --build /build
        dbus-run-session -- ctest --test-dir /build --output-on-failure -j "$(nproc)" "$@"
    ' bash "$@"
}

command="${1:-}"
[ $# -gt 0 ] && shift

case "${command}" in
list)
    for entry in "${distros[@]}"; do
        IFS='|' read -r name family base extra <<< "${entry}"
        printf '%-20s %-7s %s %s\n' "${name}" "${family}" "${base}" "${extra}"
    done
    ;;
images)
    [ $# -gt 0 ] || set -- $(all_names)
    for name in "$@"; do
        build_images "${name}"
    done
    ;;
test)
    names=()
    while [ $# -gt 0 ] && [ "$1" != -- ]; do
        names+=("$1")
        shift
    done
    [ $# -gt 0 ] && shift
    [ ${#names[@]} -gt 0 ] || mapfile -t names < <(all_names)
    failed=()
    for name in "${names[@]}"; do
        run_tests "${name}" "$@" || failed+=("${name}")
    done
    if [ ${#failed[@]} -gt 0 ]; then
        die "failed: ${failed[*]}"
    fi
    ;;
shell)
    [ $# -eq 1 ] || die "usage: containers.sh shell DISTRO"
    lookup "$1" > /dev/null
    tty_args=(-it)
    run_in_container "$1" bash
    ;;
*)
    sed -n '/^# Builds/,/^# \.\.\/build-containers/p' "$0" | sed 's/^# \{0,1\}//'
    [ -z "${command}" ] || exit 1
    ;;
esac
