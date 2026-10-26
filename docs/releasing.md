<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Releasing Orcinus

How Orcinus follows Dolphin, and the steps of a release. Every release follows this list.

## Following Dolphin (upstream)

Orcinus builds on Dolphin's `master` branch (remote `upstream` =
`https://invent.kde.org/system/dolphin.git`). There are no stable branches; a hotfix branch is
created from a release tag only when a fix cannot wait for the next release.

Upstream is merged every two to four weeks and always before a release:

1. `git fetch upstream` and `git merge upstream/master` on `master`.
2. Resolve conflicts. Orcinus' changes in Dolphin files are marked (`Whale:` comments) and kept
   small; Dolphin's own behavior wins wherever Orcinus does not deliberately differ.
3. New `src/settings/*.upd` files from upstream (configuration migrations run by
   `kconf_update`): port each one as `orcinus_*.upd` that targets `orcinusrc`, with renamed helper
   programs, so it never collides with Dolphin's. Orcinus' own migrations are code run at start-up
   instead (`kconf_update` runs only in Plasma sessions).
4. New user-visible strings: run `po-orcinus/update-catalogs.sh`; strings that Dolphin's catalogs
   do not contain yet are translated in `po-orcinus/` until KDE's translations arrive.
5. Build the development and the system configuration and run the full test suite including the
   end-to-end tests of the development automation (`WHALE_ENABLE_DEV_MCP=ON`).
6. Use the result for a few days before releasing it.

## Release checklist

1. **Upstream:** merge as above, unless the last merge is less than a week old.
2. **Strings:** the string freeze applies from the first beta. Run
   `po-orcinus/update-catalogs.sh`; every catalog must be complete
   (`msgfmt --statistics po-orcinus/*/orcinus.po`).
3. **Version:** set `WHALE_VERSION` in the top-level `CMakeLists.txt` (semantic versioning:
   bug fixes raise the patch level, new features the minor version).
4. **Changelog:** move the entries of the release in `CHANGELOG.md` under the new version with
   its date.
5. **Metainfo:** add a `<release version="…" date="…">` entry with a short description to
   `src/io.github.arkarit.orcinus.metainfo.xml`; `appstreamcli validate` must pass.
6. **Checks:** CI green on the release commit; locally
   `cmake -DBUILD_DIR=<release build> -P cmake/WhaleReleaseCheck.cmake` passes; `reuse lint`
   passes.
7. **Tag:** `git tag -a vX.Y.Z -m "Orcinus X.Y.Z"` on `master`, push the tag.
8. **Source tarball:** `git archive --prefix=orcinus-X.Y.Z/ -o orcinus-X.Y.Z.tar.xz vX.Y.Z`,
   plus `sha256sum orcinus-X.Y.Z.tar.xz > orcinus-X.Y.Z.tar.xz.sha256`; optionally sign it
   (`gpg --armor --detach-sign`).
9. **GitHub release:** create it from the tag with the changelog section as text and the tarball,
   checksum (and signature) attached.
10. **Packages:** update the packaging recipes to the new version (see the packaging notes).
11. **After the release:** add a new "unreleased" section to `CHANGELOG.md`.
