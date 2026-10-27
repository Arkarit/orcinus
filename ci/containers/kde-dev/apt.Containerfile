# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: CC0-1.0
#
# Generic Qt 6 / KDE Frameworks 6 development image for Debian, Ubuntu and KDE neon: compiler,
# CMake, ECM, Qt, the common frameworks and what offscreen tests need (D-Bus, Breeze, fonts).
# Not specific to Orcinus; see ../README.md.
#
# BASE_IMAGE: the distribution image (debian:testing, ubuntu:26.04, ...).
# NEON=1: add the KDE neon user archive on top of Ubuntu 24.04.

ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ARG NEON=0
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# Debian and Ubuntu ship Qt's SVG image plugin separately; without it no Breeze icon loads. KDE
# neon has no such package, its plugin comes with the SVG library.
RUN svg_plugins=qt6-svg-plugins \
 && apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl \
 && if [ "${NEON}" = 1 ]; then \
      svg_plugins= \
      && curl -fsSL --retry 10 --retry-all-errors https://archive.neon.kde.org/public.key -o /etc/apt/keyrings/neon.asc \
      && echo "deb [signed-by=/etc/apt/keyrings/neon.asc] http://archive.neon.kde.org/user noble main" \
           > /etc/apt/sources.list.d/neon.list \
      && apt-get update && apt-get full-upgrade -y; \
    fi \
 && apt-get install -y --no-install-recommends ${svg_plugins} \
      build-essential cmake ninja-build git pkg-config gettext clang-format \
      extra-cmake-modules qt6-base-dev qt6-base-private-dev qt6-svg-dev qt6-image-formats-plugins \
      libkf6kcmutils-dev libkf6newstuff-dev libkf6coreaddons-dev libkf6i18n-dev \
      libkf6dbusaddons-dev libkf6bookmarks-dev libkf6config-dev libkf6kio-dev libkf6parts-dev \
      libkf6solid-dev libkf6iconthemes-dev libkf6completion-dev libkf6textwidgets-dev \
      libkf6notifications-dev libkf6crash-dev libkf6windowsystem-dev libkf6widgetsaddons-dev \
      libkf6codecs-dev libkf6guiaddons-dev libkf6colorscheme-dev libkf6xmlgui-dev \
      dbus-daemon dbus-x11 xauth \
      breeze breeze-icon-theme fonts-dejavu-core fonts-noto-core \
 && rm -rf /var/lib/apt/lists/*
