# SPDX-FileCopyrightText: 2026 Orcinus contributors
# SPDX-License-Identifier: CC0-1.0
#
# Orcinus' own dependencies on top of ../kde-dev/apt.Containerfile: metadata (Baloo),
# video (libmpv), Qt Multimedia, PackageKit, DocTools and the thumbnailers the tests use.

ARG KDE_DEV_IMAGE
FROM ${KDE_DEV_IMAGE}

# KDE neon names the Baloo development packages differently from Debian and Ubuntu.
RUN if [ -f /etc/apt/sources.list.d/neon.list ]; then \
      baloo="kf6-baloo-dev kf6-baloowidgets-dev"; \
    else \
      baloo="libkf6baloo-dev libbaloowidgets-dev"; \
    fi \
 && apt-get update \
 && apt-get install -y --no-install-recommends ${baloo} \
      libkf6doctools-dev libkf6filemetadata-dev \
      libpackagekitqt6-dev libmpv-dev qt6-multimedia-dev kio-extras kdegraphics-thumbnailers \
 && rm -rf /var/lib/apt/lists/*
