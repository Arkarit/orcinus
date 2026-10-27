# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: CC0-1.0
#
# Orcinus' own dependencies on top of ../kde-dev/pacman.Containerfile: metadata (Baloo),
# video (libmpv), Qt Multimedia, PackageKit, DocTools and the thumbnailers the tests use.

ARG KDE_DEV_IMAGE
FROM ${KDE_DEV_IMAGE}

RUN pacman -Syu --noconfirm --needed \
      kdoctools kfilemetadata baloo baloo-widgets packagekit-qt6 mpv qt6-multimedia \
      kio-extras kdegraphics-thumbnailers \
 && pacman -Scc --noconfirm
