# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Runs in an orcinus-dev container as root (containers.sh release-test): release build of /src
# into /build, check that no installed file belongs to a distribution package, install into
# /usr, then ci/release-test.sh as the user "tester". FAMILY names the package manager.

set -euo pipefail

/src/ci/release-test-prepare.sh "${FAMILY}"

cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF > /build/configure.log 2>&1 || { cat /build/configure.log; exit 1; }
cmake --build /build > /build/build.log 2>&1 || { tail -50 /build/build.log; exit 1; }
cmake -DBUILD_DIR=/build -P /src/cmake/WhaleReleaseCheck.cmake

owner() {
    case "${FAMILY}" in
    apt) { dpkg -S "$1" 2> /dev/null || true; } | cut -d: -f1 ;;
    dnf | zypper) { rpm -qf "$1" 2> /dev/null || true; } | { grep -v "is not owned" || true; } ;;
    pacman) pacman -Qoq "$1" 2> /dev/null || true ;;
    esac
}

stage=/tmp/stage
rm -rf "${stage}"
DESTDIR="${stage}" cmake --install /build > /dev/null
collisions=0
while IFS= read -r file; do
    file="${file#.}"
    package="$(owner "${file}")"
    if [ -n "${package}" ]; then
        echo "Collision: ${file} belongs to ${package}" >&2
        collisions=1
    elif [ -e "${file}" ]; then
        echo "Collision: ${file} already exists" >&2
        collisions=1
    fi
done < <(cd "${stage}" && find . ! -type d)
[ "${collisions}" = 0 ] || exit 1
echo "PASS: no installed file collides with a distribution package"

cmake --install /build > /dev/null
su tester -c /src/ci/release-test.sh
