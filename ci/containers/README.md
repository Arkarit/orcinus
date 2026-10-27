<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Distribution test containers

Podman images to build and test Orcinus on the distributions it targets (release plan RL4).
They are reused for packaging (RL5) and the distribution tests (RL6).

| Name | Base | KDE Frameworks / Qt (2026-10) | Note |
|---|---|---|---|
| `ubuntu-26.04` | Ubuntu 26.04 LTS | 6.24 / 6.10 | at the minimum versions |
| `ubuntu-26.10` | Ubuntu 26.10 | 6.29 / 6.11 | |
| `debian-testing` | Debian testing | 6.30 / 6.11 | |
| `neon` | Ubuntu 24.04 + KDE neon | 6.31 / 6.11 | |
| `fedora-45` | Fedora 45 | 6.31 / 6.12 | |
| `opensuse-tumbleweed` | openSUSE Tumbleweed | 6.31 / 6.12 | |
| `arch` | Arch Linux | 6.31 / 6.12 | same as the GitHub CI |

Not supported: Debian 13 (KDE Frameworks 6.13; Orcinus needs 6.23).

## Two layers

- `kde-dev/<family>.Containerfile` → `localhost/kde-dev:<name>`: a generic Qt 6 / KDE Frameworks
  6 development image (compiler, CMake, ECM, the common frameworks, D-Bus, Breeze and fonts for
  offscreen tests). Nothing in it is specific to Orcinus, so other KDE or Qt projects can build
  on it with their own layer.
- `orcinus/<family>.Containerfile` → `localhost/orcinus-dev:<name>`: what only Orcinus needs on
  top (Baloo, libmpv, Qt Multimedia, PackageKit, DocTools, thumbnailers).

One Containerfile serves each package manager family (apt, dnf, zypper, pacman); the
distribution is chosen by the base image in `containers.sh`.

## Use

```bash
ci/containers/containers.sh images                 # build all images (or name some)
ci/containers/containers.sh test ubuntu-26.04      # build this checkout and run the tests
ci/containers/containers.sh test arch -- -R kfileitemmodeltest
ci/containers/containers.sh shell fedora-45        # interactive shell, checkout at /src
```

Each container gets a copy of the working tree without `.git` at `/src` (configuring writes into
the source directory, which must not change the real checkout) and its build directory at
`/build`; both live in `../build-containers/<name>` next to the checkout
(`ORCINUS_CONTAINER_BUILD_ROOT` changes that). Containers run rootless as the calling user, with
a fresh home directory, so nothing touches the real configuration.

Each image needs about 2–4 GB, each Release build about 1 GB. `podman rmi` or
`podman image prune` removes images; refresh them with `containers.sh images` (it pulls newer
base images).

## Own use of the images

An image never changes when it is used; every `podman run` starts a fresh copy. For a longer
lived environment, create a named container from it (`podman run --name … -it
localhost/kde-dev:fedora-45 bash`, later `podman start -ai …`), or tag a copy
(`podman tag localhost/kde-dev:fedora-45 localhost/my-fedora`).

[distrobox](https://distrobox.it/) can use the images too and runs GUI programs from them on
the desktop. It shares the real home directory by default, so a program started there uses
the real configuration (for Orcinus: `~/.config/orcinusrc`). Give such a box its own home:

```bash
distrobox create --image localhost/orcinus-dev:fedora-45 --name orcinus-fedora \
    --home ~/distrobox/orcinus-fedora
```
