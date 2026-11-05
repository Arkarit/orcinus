#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Builds the Ubuntu source packages of Orcinus for the PPA (release plan RL5), test-builds them
# like Launchpad does, and uploads them. Needs Podman for "source" and "test"; signing and
# uploading run on the host (devscripts, dput).
#
#   ubuntu-ppa.sh source [REF]          source packages for all series from the Git REF (default HEAD)
#   ubuntu-ppa.sh test [SERIES...]      build the binary package from the source package in a clean
#                                       Ubuntu container, run lintian, install it next to Dolphin and
#                                       run ci/release-test.sh
#   ubuntu-ppa.sh upload PPA [SERIES...] sign and upload, e.g. PPA = ppa:arkarit/orcinus-testing
#
# REF is a release tag (vX.Y.Z) or any commit; a commit that is not the tag of WHALE_VERSION gets
# the upstream version X.Y.Z~gitYYYYMMDD.HASH, which sorts before the release X.Y.Z.
# Package version: UPSTREAM-0ppaN~SERIES1 (N = $ORCINUS_PPA_REVISION, default 1).
# Output: $ORCINUS_PACKAGE_ROOT/ubuntu, by default ../build-packages/ubuntu next to the checkout.
# Changelog identity: $DEBFULLNAME / $DEBEMAIL, otherwise the Maintainer field of debian/control.
# Signing key for upload: $ORCINUS_SIGNING_KEY (key ID or address), otherwise gpg's default key.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "${here}/.." && pwd)"
out_root="${ORCINUS_PACKAGE_ROOT:-$(dirname "${source_dir}")/build-packages}/ubuntu"
ppa_revision="${ORCINUS_PPA_REVISION:-1}"

# Ubuntu version | series (codename)
all_series=(
    "26.04|resolute"
    "26.10|stonking"
)

die() {
    echo "ubuntu-ppa.sh: $*" >&2
    exit 1
}

series_version() {
    local entry
    for entry in "${all_series[@]}"; do
        if [ "${entry#*|}" = "$1" ]; then
            echo "${entry%%|*}"
            return
        fi
    done
    die "unknown series '$1' (known: ${all_series[*]#*|})"
}

all_series_names() {
    local entry
    for entry in "${all_series[@]}"; do
        echo "${entry#*|}"
    done
}

# The upstream version of the source package for this run, from the last "source" call.
current_upstream_version() {
    [ -f "${out_root}/upstream-version" ] || die "no source packages yet (run: ubuntu-ppa.sh source)"
    cat "${out_root}/upstream-version"
}

build_source() {
    local ref="${1:-HEAD}"
    local commit version upstream tag_commit maintainer date
    commit="$(git -C "${source_dir}" rev-parse --verify "${ref}^{commit}")"
    version="$(git -C "${source_dir}" show "${commit}:CMakeLists.txt" \
        | sed -n 's/^set(WHALE_VERSION "\(.*\)")$/\1/p')"
    [ -n "${version}" ] || die "WHALE_VERSION not found in ${ref}"
    tag_commit="$(git -C "${source_dir}" rev-parse --verify -q "v${version}^{commit}" || true)"
    if [ "${tag_commit}" = "${commit}" ]; then
        upstream="${version}"
    else
        upstream="${version}~git$(git -C "${source_dir}" show -s --format=%cd --date=format:%Y%m%d "${commit}").$(git -C "${source_dir}" rev-parse --short=9 "${commit}")"
    fi
    if [ -n "${DEBFULLNAME:-}" ] && [ -n "${DEBEMAIL:-}" ]; then
        maintainer="${DEBFULLNAME} <${DEBEMAIL}>"
    else
        maintainer="$(sed -n 's/^Maintainer: //p' "${here}/debian/control")"
    fi
    date="$(LC_ALL=C date -R)"

    echo "=== Orcinus ${upstream} from ${commit:0:12}"
    rm -rf "${out_root}"
    mkdir -p "${out_root}"
    local orig="${out_root}/orcinus_${upstream}.orig.tar.xz"
    git -C "${source_dir}" archive --format=tar --prefix="orcinus-${upstream}/" "${commit}" | xz -T0 -6 > "${orig}"
    echo "${upstream}" > "${out_root}/upstream-version"

    local series
    for series in $(all_series_names); do
        local package_version="${upstream}-0ppa${ppa_revision}~${series}1"
        local work="${out_root}/work-${series}"
        echo "=== ${series}: orcinus ${package_version}"
        mkdir -p "${work}"
        tar -xf "${orig}" -C "${work}"
        local tree="${work}/orcinus-${upstream}"
        # The packaging comes from the same commit as the sources, never from the working tree.
        rm -rf "${tree}/debian"
        cp -r "${tree}/packaging/debian" "${tree}/debian"
        cat > "${tree}/debian/changelog" << EOF
orcinus (${package_version}) ${series}; urgency=medium

  * Orcinus ${upstream} for Ubuntu $(series_version "${series}").

 -- ${maintainer}  ${date}
EOF
        ln -s "../orcinus_${upstream}.orig.tar.xz" "${work}/orcinus_${upstream}.orig.tar.xz"
        # debian/rules clean needs debhelper, so the source package is built in the series'
        # own Ubuntu image; it stays unsigned until "upload".
        podman run --rm -v "${out_root}:/out" -w "/out/work-${series}/orcinus-${upstream}" \
            "docker.io/library/ubuntu:$(series_version "${series}")" bash -c '
                set -e
                export DEBIAN_FRONTEND=noninteractive
                apt-get update -qq && apt-get install -y -qq --no-install-recommends dpkg-dev debhelper > /dev/null
                dpkg-buildpackage -S -sa -d -us -uc
            '
        mv "${work}"/orcinus_"${package_version}"* "${out_root}/"
        rm -rf "${work}"
    done
    echo "=== Source packages in ${out_root}"
    ls -1 "${out_root}"
}

# Builds the binary package like a Launchpad builder: a clean image of the series with only the
# Build-Depends of the source package (unlike Launchpad, the build itself still has network).
test_series() {
    local series="$1" version upstream dsc
    version="$(series_version "${series}")"
    upstream="$(current_upstream_version)"
    dsc="$(cd "${out_root}" && ls orcinus_"${upstream}"-0ppa*~"${series}"1.dsc)"
    echo "=== ${series}: binary build of ${dsc}"
    mkdir -p "${out_root}/test-${series}"
    podman run --rm -v "${out_root}:/out" -v "${source_dir}/ci:/ci:ro" -w /tmp -e DSC="${dsc}" -e SERIES="${series}" \
        "docker.io/library/ubuntu:${version}" bash -c '
            set -e
            export DEBIAN_FRONTEND=noninteractive
            # Dolphin, locale, test user; keeps the translations of the package installed below.
            /ci/release-test-prepare.sh apt
            apt-get install -y -qq --no-install-recommends dpkg-dev lintian > /dev/null
            dpkg-source -x "/out/${DSC}" src > /dev/null
            cd src
            apt-get build-dep -y -qq --no-install-recommends ./ > /dev/null
            dpkg-buildpackage -b -us -uc -j"$(nproc)" > /out/test-${SERIES}/build.log 2>&1 \
                || { tail -50 /out/test-${SERIES}/build.log; exit 1; }
            cd ..
            cp ./*.deb ./*.buildinfo ./*.changes "/out/test-${SERIES}/"
            echo "--- lintian"
            lintian --info --display-info ./*.changes "/out/${DSC}" | tee /out/test-${SERIES}/lintian.log || true
            echo "--- contents"
            dpkg-deb -c ./orcinus_*.deb | grep -v /locale/ | awk "{print \$6, \$7, \$8}"
            echo "--- install next to Dolphin"
            apt-get install -y -qq --no-install-recommends ./orcinus_*.deb > /out/test-${SERIES}/install.log 2>&1 \
                || { tail -30 /out/test-${SERIES}/install.log; exit 1; }
            echo "--- RUNPATHs"
            for f in $(dpkg -L orcinus); do
                [ -f "$f" ] && readelf -d "$f" 2>/dev/null | sed -n "s|.*RUNPATH.*\[\(.*\)\]|$f: \1|p"
            done
            echo "--- missing libraries"
            missing=0
            for f in $(dpkg -L orcinus); do
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

upload() {
    local ppa="$1"
    shift
    [ $# -gt 0 ] || set -- $(all_series_names)
    command -v debsign > /dev/null || die "debsign missing (sudo apt install devscripts)"
    command -v dput > /dev/null || die "dput missing (sudo apt install dput)"
    local upstream series changes
    upstream="$(current_upstream_version)"
    for series in "$@"; do
        changes="${out_root}/orcinus_${upstream}-0ppa${ppa_revision}~${series}1_source.changes"
        [ -f "${changes}" ] || die "missing ${changes}"
        debsign ${ORCINUS_SIGNING_KEY:+-k"${ORCINUS_SIGNING_KEY}"} "${changes}"
        dput "${ppa}" "${changes}"
    done
}

command="${1:-}"
[ $# -gt 0 ] && shift
case "${command}" in
source)
    [ $# -le 1 ] || die "usage: ubuntu-ppa.sh source [REF]"
    build_source "$@"
    ;;
test)
    [ $# -gt 0 ] || set -- $(all_series_names)
    failed=()
    for series in "$@"; do
        test_series "${series}" || failed+=("${series}")
    done
    [ ${#failed[@]} -eq 0 ] || die "failed: ${failed[*]}"
    ;;
upload)
    [ $# -ge 1 ] || die "usage: ubuntu-ppa.sh upload PPA [SERIES...]"
    upload "$@"
    ;;
*)
    sed -n '/^# Builds the Ubuntu/,/^# Signing/p' "$0" | sed 's/^# \{0,1\}//'
    [ -z "${command}" ] || exit 1
    ;;
esac
