# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: CC0-1.0
#
# Generic Qt 6 / KDE Frameworks 6 development image for Fedora; see apt.Containerfile.
#
# BASE_IMAGE: the distribution image (registry.fedoraproject.org/fedora:45, ...).

ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ENV LANG=C.UTF-8

RUN dnf -y upgrade \
 && dnf -y install --setopt=install_weak_deps=False \
      gcc-c++ cmake ninja-build git pkgconf gettext clang-tools-extra git-clang-format \
      extra-cmake-modules 'cmake(Qt6Core)' 'cmake(Qt6Widgets)' 'cmake(Qt6DBus)' \
      'cmake(Qt6Concurrent)' 'cmake(Qt6GuiPrivate)' 'cmake(Qt6Svg)' 'cmake(Qt6Test)' \
      qt6-qtimageformats \
      'cmake(KF6KCMUtils)' 'cmake(KF6NewStuff)' 'cmake(KF6CoreAddons)' 'cmake(KF6I18n)' \
      'cmake(KF6DBusAddons)' 'cmake(KF6Bookmarks)' 'cmake(KF6Config)' 'cmake(KF6KIO)' \
      'cmake(KF6Parts)' 'cmake(KF6Solid)' 'cmake(KF6IconThemes)' 'cmake(KF6Completion)' \
      'cmake(KF6TextWidgets)' 'cmake(KF6Notifications)' 'cmake(KF6Crash)' \
      'cmake(KF6WindowSystem)' 'cmake(KF6WidgetsAddons)' 'cmake(KF6Codecs)' \
      'cmake(KF6GuiAddons)' 'cmake(KF6ColorScheme)' 'cmake(KF6XmlGui)' \
      dbus-daemon dbus-x11 xorg-x11-xauth \
      plasma-breeze-qt6 kf6-breeze-icons dejavu-sans-fonts google-noto-sans-fonts \
 && dnf clean all
