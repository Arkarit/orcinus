#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Installs Orcinus system-wide under /usr from its own build directory ../build-whale-system.
#
# A developer helper for the maintainer's setup, not an installer for users: it assumes
# Debian/Ubuntu (dpkg for the collision check) on x86_64 and the build directory next to the
# checkout. Distributions use their own packages instead.
#
# Never install into /usr from ../build-whale: that binary has a RUNPATH pointing to the private
# install prefix, and its data directory is compiled in.
#
# Steps: configure (only the first time), build, stage into a temporary DESTDIR, check that no
# staged file already exists or belongs to a package (e.g. Dolphin), then install with sudo and
# refresh the desktop, icon and KService caches.
#
# Uninstall: sudo xargs rm < ../build-whale-system/install_manifest.txt

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
build_dir="$(cd -- "${script_dir}/.." && pwd -P)/build-whale-system"
prefix=/usr

stage_dir="$(mktemp -d)"
trap 'rm -rf "${stage_dir}" "${stage_dir}.previous-manifest"' EXIT

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
    echo "==> Configuring ${build_dir}"
    cmake -S "${script_dir}" -B "${build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX="${prefix}" \
        -DBUILD_TESTING=OFF
fi

configured_prefix="$(sed -n 's/^CMAKE_INSTALL_PREFIX:PATH=//p' "${build_dir}/CMakeCache.txt")"
if [[ "${configured_prefix}" != "${prefix}" ]]; then
    echo "Error: ${build_dir} is configured for prefix '${configured_prefix}', expected '${prefix}'." >&2
    exit 1
fi

echo "==> Building"
cmake --build "${build_dir}"

# Files that the previous installation of Orcinus put there may be replaced. Everything else must not
# exist yet and must not belong to a package. Save the manifest first, staging overwrites it.
previous_manifest="${stage_dir}.previous-manifest"
if [[ -f "${build_dir}/install_manifest.txt" ]]; then
    cp "${build_dir}/install_manifest.txt" "${previous_manifest}"
else
    : > "${previous_manifest}"
fi

echo "==> Staging the installation"
DESTDIR="${stage_dir}" cmake --install "${build_dir}" > /dev/null

collisions=0
while IFS= read -r file; do
    file="${file#.}"
    if dpkg -S "${file}" > /dev/null 2>&1; then
        echo "Collision: ${file} belongs to package $(dpkg -S "${file}" 2>/dev/null | cut -d: -f1)" >&2
        collisions=1
    elif [[ -e "${file}" || -L "${file}" ]] && ! grep -qxF "${file}" "${previous_manifest}"; then
        echo "Collision: ${file} already exists and was not installed by Orcinus" >&2
        collisions=1
    fi
done < <(cd "${stage_dir}" && find . ! -type d)

# Installed files may only carry RUNPATHs relative to themselves ($ORIGIN, the private library
# directory of B2/B3), never an absolute path such as the private development prefix.
while IFS= read -r file; do
    runpath="$(readelf -d "${file}" 2>/dev/null | sed -n 's/.*R\(UN\)\{0,1\}PATH.*\[\(.*\)\]/\2/p')"
    [[ -z "${runpath}" ]] && continue
    IFS=: read -ra entries <<< "${runpath}"
    for entry in "${entries[@]}"; do
        if [[ "${entry}" != '$ORIGIN'* ]]; then
            echo "Collision check: ${file#"${stage_dir}"} has the absolute RUNPATH entry ${entry}." >&2
            collisions=1
        fi
    done
done < <(find "${stage_dir}" -type f \( -name '*.so*' -o -path '*/bin/*' \))

# B2/B3: the private libraries must be there and be found by the binary and liborcinusprivate.
private_dir="${stage_dir}${prefix}/lib/x86_64-linux-gnu/orcinus"
view_api="$(readelf -d "${stage_dir}${prefix}/bin/orcinus" | grep -o 'liborcinusviewapi\.so\.[0-9]*')"
for library in libdolphinvcs.so.6 "${view_api:-liborcinusviewapi}"; do
    if [[ ! -e "${private_dir}/${library}" ]]; then
        echo "Check: the private library ${library} is missing in ${private_dir#"${stage_dir}"}." >&2
        collisions=1
    fi
done
if ! readelf -d "${stage_dir}${prefix}/bin/orcinus" | grep -qF '[$ORIGIN/../lib/x86_64-linux-gnu/orcinus]' \
    || ! readelf -d "${stage_dir}${prefix}/lib/x86_64-linux-gnu/liborcinusprivate.so.6" | grep -qF '[$ORIGIN/orcinus]'; then
    echo "Check: the staged binary or liborcinusprivate does not find the private library directory." >&2
    collisions=1
fi

if [[ ${collisions} -ne 0 ]]; then
    echo "Aborting, nothing was installed." >&2
    exit 1
fi
echo "    $(cd "${stage_dir}" && find . ! -type d | wc -l) files, no collisions"

if pgrep -x orcinus > /dev/null; then
    echo "Warning: Orcinus is running. Close it to use the new version after installation." >&2
fi

echo "==> Installing to ${prefix} (sudo)"
sudo cmake --install "${build_dir}"
sudo chown "$(id -un):$(id -gn)" "${build_dir}/install_manifest.txt"

echo "==> Refreshing caches"
sudo update-desktop-database "${prefix}/share/applications"
sudo gtk-update-icon-cache -q "${prefix}/share/icons/hicolor"
kbuildsycoca6 > /dev/null 2>&1

echo "==> Done: $(command -v orcinus)"
