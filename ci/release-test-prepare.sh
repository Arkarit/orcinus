#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Prepares a distribution container for ci/release-test.sh (release plan RL6). Runs as root:
# installs Dolphin, xdg-utils and the D-Bus tools, makes package installations keep their
# translations (container images strip /usr/share/locale), creates the de_DE.UTF-8 locale and
# the test user "tester".
#
#   release-test-prepare.sh FAMILY      FAMILY = apt | dnf | zypper | pacman
#
# Run it before installing an Orcinus package, so the package's translations are installed too.

set -euo pipefail

family="${1:?usage: release-test-prepare.sh apt|dnf|zypper|pacman}"

case "${family}" in
apt)
    export DEBIAN_FRONTEND=noninteractive
    rm -f /etc/dpkg/dpkg.cfg.d/excludes
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends dolphin xdg-utils locales dbus-daemon dbus-bin > /dev/null
    localedef -i de_DE -f UTF-8 de_DE.UTF-8
    ;;
dnf)
    dnf install -y -q dolphin xdg-utils glibc-langpack-de dbus-daemon dbus-tools util-linux > /dev/null
    ;;
zypper)
    zypper -n -q install --no-recommends dolphin xdg-utils glibc-locale dbus-1-tools util-linux > /dev/null
    ;;
pacman)
    sed -i '/^NoExtract/d' /etc/pacman.conf
    # glibc again, now with the locale sources the image left out.
    pacman -Syu --noconfirm --needed dolphin xdg-utils > /dev/null
    pacman -S --noconfirm glibc > /dev/null
    localedef -i de_DE -f UTF-8 de_DE.UTF-8
    ;;
*)
    echo "release-test-prepare.sh: unknown family '${family}'" >&2
    exit 2
    ;;
esac

id tester > /dev/null 2>&1 || useradd -m tester
