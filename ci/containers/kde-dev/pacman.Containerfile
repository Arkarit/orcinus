# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: CC0-1.0
#
# Generic Qt 6 / KDE Frameworks 6 development image for Arch Linux; see apt.Containerfile.
#
# BASE_IMAGE: the distribution image (archlinux:latest).

ARG BASE_IMAGE
FROM ${BASE_IMAGE}

ENV LANG=C.UTF-8

RUN pacman -Syu --noconfirm --needed \
      base-devel git cmake ninja clang python gettext dbus xorg-xauth \
      extra-cmake-modules qt6-base qt6-svg qt6-imageformats \
      kcmutils knewstuff kcoreaddons ki18n kdbusaddons kbookmarks kconfig kio kparts solid \
      kiconthemes kcompletion ktextwidgets knotifications kcrash kwindowsystem kwidgetsaddons \
      kcodecs kguiaddons kcolorscheme kxmlgui \
      breeze breeze-icons ttf-dejavu noto-fonts \
 && pacman -Scc --noconfirm
