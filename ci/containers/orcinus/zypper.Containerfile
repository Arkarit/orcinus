# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: CC0-1.0
#
# Orcinus' own dependencies on top of ../kde-dev/zypper.Containerfile: metadata (Baloo),
# video (libmpv), Qt Multimedia, PackageKit, DocTools and the thumbnailers the tests use.

ARG KDE_DEV_IMAGE
FROM ${KDE_DEV_IMAGE}

RUN zypper --non-interactive --gpg-auto-import-keys refresh \
 && zypper --non-interactive install --no-recommends \
      'cmake(KF6DocTools)' 'cmake(KF6FileMetaData)' 'cmake(KF6Baloo)' 'cmake(KF6BalooWidgets)' \
      packagekitqt6-devel 'pkgconfig(mpv)' 'cmake(Qt6Multimedia)' 'cmake(Qt6MultimediaWidgets)' \
      'cmake(Qt6OpenGLWidgets)' kf6-baloo-kioslaves kio-extras kdegraphics-thumbnailers \
 && zypper clean --all
