# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: CC0-1.0
#
# Orcinus' own dependencies on top of ../kde-dev/dnf.Containerfile: metadata (Baloo),
# video (libmpv), Qt Multimedia, PackageKit, DocTools and the thumbnailers the tests use.

ARG KDE_DEV_IMAGE
FROM ${KDE_DEV_IMAGE}

RUN dnf -y install --setopt=install_weak_deps=False \
      'cmake(KF6DocTools)' 'cmake(KF6FileMetaData)' 'cmake(KF6Baloo)' 'cmake(KF6BalooWidgets)' \
      'cmake(packagekitqt6)' 'pkgconfig(mpv)' 'cmake(Qt6Multimedia)' 'cmake(Qt6MultimediaWidgets)' \
      'cmake(Qt6OpenGLWidgets)' kio-extras kdegraphics-thumbnailers \
 && dnf clean all
