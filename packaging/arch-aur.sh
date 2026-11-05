#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Tests the AUR recipes of Orcinus (packaging/arch/orcinus, packaging/arch/orcinus-git; release
# plan RL5) and prepares them for the AUR. Needs Podman.
#
#   arch-aur.sh test [PACKAGE...]       build each recipe in a clean Arch container, run namcap,
#                                       install it next to Dolphin and run ci/release-test.sh
#   arch-aur.sh release TARBALL         set pkgver and sha256sums of orcinus to the release
#                                       tarball orcinus-X.Y.Z.tar.xz (docs/releasing.md, step 8)
#   arch-aur.sh srcinfo [PACKAGE...]    PKGBUILD plus generated .SRCINFO per package in OUTPUT/aur,
#                                       the files an AUR repository holds
#
# The tests never download Orcinus: orcinus gets a tarball of HEAD under the name of the release
# tarball, orcinus-git a bare clone of the local master branch instead of the GitHub repository.
# Output: $ORCINUS_PACKAGE_ROOT/arch, by default ../build-packages/arch next to the checkout.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "${here}/.." && pwd)"
out_root="${ORCINUS_PACKAGE_ROOT:-$(dirname "${source_dir}")/build-packages}/arch"
image=docker.io/library/archlinux:latest
all_packages=(orcinus orcinus-git)

die() {
    echo "arch-aur.sh: $*" >&2
    exit 1
}

pkgbuild_value() {
    sed -n "s/^$2=//p" "${here}/arch/$1/PKGBUILD"
}

test_package() {
    local package="$1" work="${out_root}/$1"
    [ -f "${here}/arch/${package}/PKGBUILD" ] || die "unknown package '${package}'"
    echo "=== ${package}"
    rm -rf "${work}"
    mkdir -p "${work}/recipe"
    cp "${here}/arch/${package}/PKGBUILD" "${work}/recipe/"
    case "${package}" in
    orcinus)
        local version
        version="$(pkgbuild_value orcinus pkgver)"
        # makepkg uses a source file that is already there instead of downloading it.
        git -C "${source_dir}" archive --format=tar --prefix="orcinus-${version}/" HEAD \
            | xz -T0 -6 > "${work}/recipe/orcinus-${version}.tar.xz"
        ;;
    orcinus-git)
        git clone -q --bare --branch master "${source_dir}" "${work}/orcinus.git"
        sed -i 's|git+https://github.com/Arkarit/orcinus.git|git+file:///work/orcinus.git|' \
            "${work}/recipe/PKGBUILD"
        ;;
    esac
    podman run --rm -v "${work}:/work" -v "${source_dir}/ci:/ci:ro" "${image}" bash -c '
        set -e
        # Dolphin, locale, test user; keeps the translations of the package installed below.
        /ci/release-test-prepare.sh pacman
        pacman -S --noconfirm --needed base-devel namcap > /dev/null
        useradd -m builder
        chown -R builder /work
        # Give everything back to the calling user (root in the rootless container).
        trap "chown -R root:root /work" EXIT
        cd /work/recipe
        source ./PKGBUILD
        pacman -S --noconfirm --needed --asdeps "${depends[@]}" "${makedepends[@]}" > /dev/null
        su builder -c "makepkg --noconfirm" > /work/build.log 2>&1 || { tail -50 /work/build.log; exit 1; }
        package_file="$(ls ./*.pkg.tar.zst | grep -v -e -debug-)"
        echo "--- built ${package_file#./}"
        echo "--- namcap"
        namcap PKGBUILD "${package_file}" | tee /work/namcap.log || true
        echo "--- contents"
        pacman -Qlp "${package_file}" | grep -v -e /locale/ -e /licenses/ -e "/$" | cut -d" " -f2
        echo "--- install next to Dolphin"
        pacman -U --noconfirm "${package_file}" > /work/install.log 2>&1 \
            || { tail -30 /work/install.log; exit 1; }
        echo "--- RUNPATHs"
        for f in $(pacman -Qlq "${pkgname}"); do
            [ -f "$f" ] && readelf -d "$f" 2>/dev/null | sed -n "s|.*RUNPATH.*\[\(.*\)\]|$f: \1|p"
        done
        echo "--- missing libraries"
        missing=0
        for f in $(pacman -Qlq "${pkgname}"); do
            if [ -f "$f" ] && readelf -h "$f" > /dev/null 2>&1; then
                if ldd "$f" | grep "not found"; then missing=1; fi
            fi
        done
        [ "${missing}" = 0 ] && echo none
        echo "--- release test (ci/release-test.sh)"
        su tester -c /ci/release-test.sh || missing=1
        [ "${missing}" = 0 ]
    '
}

set_release() {
    local tarball="$1" name version sum
    [ -f "${tarball}" ] || die "no such file: ${tarball}"
    name="$(basename "${tarball}")"
    [[ "${name}" =~ ^orcinus-([0-9][0-9A-Za-z.]*)\.tar\.xz$ ]] || die "expected orcinus-X.Y.Z.tar.xz, got ${name}"
    version="${BASH_REMATCH[1]}"
    sum="$(sha256sum "${tarball}" | cut -d' ' -f1)"
    sed -i -e "s/^pkgver=.*/pkgver=${version}/" -e "s/^pkgrel=.*/pkgrel=1/" \
        -e "s/^sha256sums=.*/sha256sums=('${sum}')/" "${here}/arch/orcinus/PKGBUILD"
    echo "orcinus ${version}, sha256 ${sum}"
}

write_srcinfo() {
    local package="$1" target="${out_root}/aur/$1"
    [ -f "${here}/arch/${package}/PKGBUILD" ] || die "unknown package '${package}'"
    mkdir -p "${target}"
    cp "${here}/arch/${package}/PKGBUILD" "${target}/"
    podman run --rm -v "${here}/arch/${package}:/recipe:ro" "${image}" bash -c '
        useradd -m builder && cp /recipe/PKGBUILD /home/builder/ && cd /home/builder \
            && su builder -c "makepkg --printsrcinfo"
    ' > "${target}/.SRCINFO"
    echo "${target}: PKGBUILD and .SRCINFO, ready for the AUR repository ${package}"
}

command="${1:-}"
[ $# -gt 0 ] && shift
case "${command}" in
test)
    [ $# -gt 0 ] || set -- "${all_packages[@]}"
    failed=()
    for package in "$@"; do
        test_package "${package}" || failed+=("${package}")
    done
    [ ${#failed[@]} -eq 0 ] || die "failed: ${failed[*]}"
    ;;
release)
    [ $# -eq 1 ] || die "usage: arch-aur.sh release TARBALL"
    set_release "$1"
    ;;
srcinfo)
    [ $# -gt 0 ] || set -- "${all_packages[@]}"
    for package in "$@"; do
        write_srcinfo "${package}"
    done
    ;;
*)
    sed -n '/^# Tests the AUR/,/^# Output/p' "$0" | sed 's/^# \{0,1\}//'
    [ -z "${command}" ] || exit 1
    ;;
esac
