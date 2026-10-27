# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: CC0-1.0
#
# Generic Qt 6 / KDE Frameworks 6 development image for openSUSE; see apt.Containerfile.
#
# BASE_IMAGE: the distribution image (registry.opensuse.org/opensuse/tumbleweed, ...).

ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ENV LANG=C.UTF-8

RUN zypper --non-interactive --gpg-auto-import-keys refresh \
 && zypper --non-interactive dup \
 && zypper --non-interactive install --no-recommends \
      gcc-c++ cmake ninja git pkgconf gettext-tools clang-tools awk \
      kf6-extra-cmake-modules 'cmake(Qt6Core)' 'cmake(Qt6Widgets)' 'cmake(Qt6DBus)' \
      'cmake(Qt6Concurrent)' 'cmake(Qt6GuiPrivate)' 'cmake(Qt6Svg)' 'cmake(Qt6Test)' \
      qt6-imageformats \
      'cmake(KF6KCMUtils)' 'cmake(KF6NewStuff)' 'cmake(KF6CoreAddons)' 'cmake(KF6I18n)' \
      'cmake(KF6DBusAddons)' 'cmake(KF6Bookmarks)' 'cmake(KF6Config)' 'cmake(KF6KIO)' \
      'cmake(KF6Parts)' 'cmake(KF6Solid)' 'cmake(KF6IconThemes)' 'cmake(KF6Completion)' \
      'cmake(KF6TextWidgets)' 'cmake(KF6Notifications)' 'cmake(KF6Crash)' \
      'cmake(KF6WindowSystem)' 'cmake(KF6WidgetsAddons)' 'cmake(KF6Codecs)' \
      'cmake(KF6GuiAddons)' 'cmake(KF6ColorScheme)' 'cmake(KF6XmlGui)' \
      dbus-1-daemon xauth \
      breeze6-style kf6-breeze-icons dejavu-fonts google-noto-sans-fonts \
 && zypper clean --all
