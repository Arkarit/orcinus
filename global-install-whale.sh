#!/usr/bin/env bash

# SPDX-License-Identifier: GPL-2.0-or-later

# Installs Whale system-wide under /usr from its own build directory ../build-whale-system.
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

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$(cd -- "${script_dir}/.." && pwd)/build-whale-system"
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

# Files that the previous installation of Whale put there may be replaced. Everything else must not
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
        echo "Collision: ${file} already exists and was not installed by Whale" >&2
        collisions=1
    fi
done < <(cd "${stage_dir}" && find . ! -type d)

if readelf -d "${stage_dir}${prefix}/bin/whale" | grep -qE 'RUNPATH|RPATH'; then
    echo "Collision check: the staged whale binary still has a RUNPATH." >&2
    collisions=1
fi

if [[ ${collisions} -ne 0 ]]; then
    echo "Aborting, nothing was installed." >&2
    exit 1
fi
echo "    $(cd "${stage_dir}" && find . ! -type d | wc -l) files, no collisions"

if pgrep -x whale > /dev/null; then
    echo "Warning: Whale is running. Close it to use the new version after installation." >&2
fi

echo "==> Installing to ${prefix} (sudo)"
sudo cmake --install "${build_dir}"
sudo chown "$(id -un):$(id -gn)" "${build_dir}/install_manifest.txt"

echo "==> Refreshing caches"
sudo update-desktop-database "${prefix}/share/applications"
sudo gtk-update-icon-cache -q "${prefix}/share/icons/hicolor"
kbuildsycoca6 > /dev/null 2>&1

echo "==> Done: $(command -v whale)"
