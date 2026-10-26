# RL: First public release (2026-10-25)

Whale is feature-complete for the first release (master 3a68c272d, full suite 99/99 green).
This plan covers everything between "feature-complete" and "announced".

This is the first `.privatedoc` document written in English, because all remaining documents
are translated to English as part of RL1 (decision D3).

## Decisions

| # | Question | Decision |
|---|---|---|
| D1 | Polishing phase? | **None.** Whale is considered polished. From now on only **fixes**, collected until the end of the beta/RC phase (RL7). |
| D2 | `.privatedoc/` in a public repo? | **Stays public.** Documents are deleted only when they become obsolete. |
| D3 | Language of `.privatedoc/` | **English.** All remaining documents are translated before the repo goes public. |
| D4 | History rewrite? | **Only for gross findings** in the audit: insults or personal data. Everything else stays in the history. |

## Blockers (handled first, before RL1)

Decisions that are expensive to change after the release.

### B1: Name (decided 2026-10-25: **Orcinus**)

"Whale" is taken in exactly our niche: `org.kde.whale` (Carl Schwan's Kirigami file manager on
invent.kde.org, since 2020), a second "Whale" file manager announced on KDE Discuss in
August 2026, the Naver Whale browser (`naver-whale-stable-bin` in the AUR), and the AUR package
name `whale` (an unofficial Trello app).

Candidates checked (AUR, Flathub, Debian/Ubuntu sources, Fedora, GitHub): Willy (English slang),
Orca (the GNOME screen reader), Orcinus, Shachi, Grampus (too close to Gramps), Blackfish (the
SeaWorld documentary), Orque, Épaulard, Spekkhogger. The user chose **Orcinus**: no package of
that name anywhere, only small dead projects on GitHub. The icon stays.

Trademark search (2026-10-25, DPMA / EUIPO / USPTO, done by the user):

- DE 302022014414 and EU 018921004 "orcinus" (word/figurative, enesty GmbH / Jurke
  Engineering GmbH, represented by patent attorneys): classes 7, 9, 11, temperature control
  technology. Class 9 covers only "software for temperature control devices / temperature
  control systems"; no class 42. Assessment: low residual risk, the goods are clearly different
  from a file manager. Not legal advice.
- EU 016842916 "NORCINUS" (Italian food/catering, classes 35, 43): irrelevant.
- US 88322629: clothing, irrelevant.

Decision (user, 2026-10-25): keep Orcinus without further safeguards (no inquiry, no lawyer).

Still open for B1: the rename plan itself (what changes, what stays internal, migration of
existing user config).

### B2: Dependency on Dolphin's system libraries

Phase 1 reuses the system `libdolphinvcs.so.6` and the `dolphin/vcs` plugins. Every package
therefore depends on an ABI-compatible Dolphin package, and Flatpak cannot work this way.
Options: accept Dolphin as a dependency, build an own VCS library that still loads Dolphin's VCS
plugins, or disable VCS cleanly when the library is missing.

**Decided 2026-10-26: option B, implemented** (see the end of this section).

#### Analysis (2026-10-26)

What VCS is here: `libdolphinvcs` contains only the plugin interface `KVersionControlPlugin`
(one class, ~240 lines). The plugins for Git, Subversion, Mercurial, Bazaar and Dropbox come from
the separate package `dolphin-plugins` and provide the status emblems and the version control
context menu entries. Only `liborcinusprivate` links the library (`NEEDED libdolphinvcs.so.6`),
so Orcinus does not start at all without it.

Facts checked:
- **Plugin loading needs one shared copy.** `VersionControlObserver::initPlugins()` loads the
  plugins with `KPluginFactory::instantiatePlugin<KVersionControlPlugin>`, i.e. `qobject_cast`.
  That works only if Orcinus and the plugin use the same loaded copy of the class. An own library
  under another name would load next to the system one (the plugins need `libdolphinvcs.so.6`)
  and every cast would fail, so "own library, other name" is not possible.
- **A private copy with the same SONAME works.** Test with the installed Orcinus, offscreen and
  a throwaway profile, Git plugin enabled, the build tree's `libdolphinvcs.so.6` (26.11.70) in a
  private directory first on the library path: only the private copy is mapped, the system's
  `fileviewgitplugin.so` (built against 25.12.3) loads and runs (10 `git` calls, the same as
  with the system library). The dynamic loader satisfies the plugin's `NEEDED` with the copy
  that is already loaded.
- **The interface is stable.** Last API change 2020 (`localRepositoryRoot`, before that a new
  pure virtual in April 2020); SOVERSION 6 since the KF6 port. We merge upstream regularly, so a
  private copy follows any future change.
- **Packaging today:** Debian/Ubuntu ship the library as the tiny package `libdolphinvcs6`
  (no dependency on `dolphin`); Fedora in `dolphin-libs` together with `libdolphinprivate`;
  Arch only inside the full `dolphin` package. `dolphin-plugins` depends on `dolphin` on all
  three, so VCS emblems need Dolphin installed in any case. Dolphin's Flatpak builds
  `dolphin-plugins` into its own bundle.

Options:

| | Option | For | Against |
|---|---|---|---|
| A | Keep: depend on the system library | No work | Arch packages pull in the full Dolphin; ABI tied to the distro's Dolphin; Flatpak/AppImage must bundle anyway |
| B | **Install a private copy** `libdolphinvcs.so.6` in `${LIBDIR}/orcinus/`, found through the RUNPATH of `liborcinusprivate` | No dependency on any Dolphin package; Dolphin's plugins still work when installed; no file collision (private directory); same build for distro packages, Flatpak and AppImage | A second copy of an 18 KB library; some distro policies frown on bundled libraries (private libraries in `/usr/lib/<package>/` are accepted by Debian) |
| C | Make VCS optional at build time (`#if`) | Smallest dependency set | Changes in upstream files (`dolphinview`, `versioncontrolobserver`, settings), merge conflicts; packagers have to choose; the feature disappears |
| D | Load the library at runtime (`dlopen`) and switch VCS off without it | Works with and without Dolphin | Most code, the interface is a C++ class with signals; still needs the system library for the plugins |

Recommendation: **B.** It removes the dependency with a few lines of CMake, keeps VCS fully
working wherever `dolphin-plugins` is installed, and is the layout every bundle format needs
anyway. The `dolphin/vcs` namespace and the plugins stay Dolphin's (no change to CLAUDE.md's rule).
Steps if chosen: install the `dolphinvcs` target (no headers, no CMake config) into
`${KDE_INSTALL_LIBDIR}/orcinus`, set `INSTALL_RPATH` `$ORIGIN/orcinus` on `liborcinusprivate`,
extend the install script's collision check and test: Orcinus starts with `libdolphinvcs6`
removed in a container (RL4) and loads the Git plugin when it is present.

Implemented (2026-10-26): `src/CMakeLists.txt` installs `dolphinvcs` into
`${KDE_INSTALL_LIBDIR}/${WHALE_COMPONENT}` (unless `WHALE_INSTALL_DOLPHIN_EXTRAS`) and gives
`liborcinusprivate` the RUNPATH `$ORIGIN/orcinus`; `global-install-orcinus.sh` refuses a staging
without it. Tested on the staged install in a mount namespace with the system
`libdolphinvcs.so.25.12.3` hidden: Orcinus starts, maps only the private copy and Dolphin's Git
plugin works (10 `git` calls); with the plugins hidden as well it starts without VCS. The
container test with Dolphin uninstalled stays in RL4.
Legal: the library is LGPL-2.0-only source from this repository (`LICENSES/LGPL-2.0-only.txt`,
SPDX headers), so no additional notice in the repository; RL5 lists it in the packages'
copyright files.

### B3: What is stable after 1.0

The style format (`Version` key) and the view plugin API (`X-Orcinus-ViewApiVersion`) are already
versioned. To decide: whether the plugin API is meant for third parties and promised stable, or
declared internal/unstable for 1.0 (proposal); and how config key changes are migrated once
users have them.

**Decided 2026-10-26:** proposals 1–4 below accepted; `liborcinusviewapi` is installed privately
(done, see the end of this section). A stable public plugin API is **not** part of 1.0.

#### Analysis (2026-10-26)

What Orcinus fixes towards the outside, and how it is handled today:

| Surface | Today | Change after 1.0 means |
|---|---|---|
| View plugin API (`liborcinusviewapi`, 21 files in `src/whale/api/`, `X-Orcinus-ViewApiVersion`) | Version 10, bumped 9 times in 3 weeks; the host accepts only exactly its own version; headers and CMake config are not installed, so only plugins built in this tree exist; SOVERSION = API version | Every bump breaks every plugin built outside the tree |
| Styles (`orcinusstylesrc`) | `Version=1`; unknown entries are kept, a newer file is read as far as understood (forward compatible) | Already prepared |
| Session and location state (`WhalePersistence`) | `FormatVersion=1`; newer data is ignored with a warning | At worst a restored session is lost |
| View preferences (`orcinusviewpreferencesrc`), per-folder view properties | View properties have upstream's version and migration; view preferences store plugin IDs | Plugin IDs must stay (`orcinus_treemap`) |
| Settings (`orcinusrc`, KConfigXT) | Upstream migrates keys with `kconf_update` scripts on `dolphinrc` (2025-04, 2025-11, 2026-06); Orcinus installs none of them, so **upstream migrations never reach `orcinusrc`**. Own migrations are ad hoc (`WhaleSplitViewStartup::migrate`, toolbar `Migrated…TB7` flags) | A merged upstream key change silently resets that setting for Orcinus users |
| Action names `whale_*` (in the user's toolbar and shortcut files) | Internal names, stored by the user | Renaming loses toolbar entries and shortcuts unless migrated |
| Command line | `--select`, `--split`, `--new-window`, `--sudo`, `--daemon` documented; `--view-style` hidden | Scripts and desktop files break |
| File names, app ID, D-Bus names | Fixed by the rename | Users' associations, pinned launchers, configs |

Proposals:
1. **Plugin API internal for 1.0.** No headers, no CMake config, exact version check stays;
   plugins only in this tree. Stated in README/metainfo ("no third-party plugin API yet").
   Optional: install `liborcinusviewapi` privately next to `libdolphinvcs` (`lib/orcinus/`), so
   distributions do not see a public library whose SONAME changes with every API bump.
2. **Stable promise for user data:** settings, styles, view preferences, toolbars and shortcuts
   survive every upgrade; only a restored session may be dropped after a format change.
   Consequences: action names `whale_*`, plugin IDs, config group and key names are frozen from
   1.0 on (renames only with a migration), and every format change bumps its version.
3. **One migration path for `orcinusrc`.** Port upstream's `.upd` files and their update
   programs to `orcinusrc` with Orcinus names (Dolphin's own ones must not be shipped, collision)
   and check, at every upstream merge, whether new `.upd` files arrived (merge checklist).
   Own migrations go the same way instead of ad hoc flags. To verify before implementing: when
   KF6 runs `kconf_update` for an application. Implementation belongs in RL3.
4. **Command line:** the documented options are stable; `--view-style` stays internal.

Why not a stable plugin API already for 1.0 (discussed 2026-10-26): technically close (most API
classes have d-pointers, the API has been quiet since 2026-10-07 apart from linked split views),
but only one real plugin (the treemap) has shaped it; public headers would carry the internal
`Whale*` class names (N1), so a freeze needs a rename of the API first; host-implemented interfaces
(`WhaleViewContext`, 12 virtuals; `WhaleFileOperations`, 15) need extension points before they
can be frozen; and headers, CMake config, docs, an example plugin and an ABI check in CI are
release work without any external user yet. **After 1.0, as its own feature:** once a second
plugin exists and the API has stayed unchanged for one or two releases, rename the API classes,
freeze it as API 1 with extension points, install headers and CMake config, add an ABI check
(e.g. libabigail). Possible earlier: a "technology preview" with installed headers and the
explicit rule that plugins are rebuilt for every Orcinus version (the exact version check exists).

Implemented (2026-10-26): `WHALE_PRIVATE_LIBDIR` (`lib/<arch>/orcinus`) and
`whale_use_private_libdir()` in the top-level CMakeLists; `liborcinusviewapi` installed there;
RUNPATHs `$ORIGIN/../lib/<arch>/orcinus` (binary), `$ORIGIN/orcinus` (`liborcinusprivate`),
`$ORIGIN/../../../../orcinus` (treemap plugin). `global-install-orcinus.sh` now rejects any
absolute RUNPATH entry and checks that the libraries the binary needs are in the private
directory. Tested on the staged install with the system `libdolphinvcs` hidden: Orcinus runs,
maps both private libraries, Dolphin's Git plugin works; the treemap resolves its API library
on its own.

## Order

1. RL1 Repo audit and legal
2. RL2 String freeze and translation
3. RL3 Release engineering
4. RL4 Test containers
5. RL5 Packaging
6. RL6 Automated tests across distros
7. RL7 Beta/RC and fixes
8. RL8 Website and documentation
9. RL9 Making the repo public
10. RL10 Announcement

Fixes found in any step go directly onto master (see the "work on master" workflow) and are
listed in the changelog draft.

## Steps

### RL1: Repo audit and legal

- Go through the full history for personal data, credentials, tokens, private paths and
  inappropriate wording. Look at commit messages, tracked files (including deleted ones) and
  binary files. Rewrite the history only for findings covered by D4.
- `.privatedoc/`: sort out obsolete documents (delete them), translate the rest to English.
  Check them for personal data in the same pass.
- Check repo-root helper scripts (`run-orcinus.sh`, `global-install-orcinus.sh`, ...) for
  machine-specific paths. Generalize or document them.
- Check `AGENTS.md` / `CLAUDE.md` for content that should not be public.
- Licensing: REUSE lint must pass; every new file has SPDX headers; Dolphin's copyright notices
  remain intact; the Whale icon has a clear license.
- Trademarks: no "Dolphin" or KDE logo in Whale's branding. README, About dialog and metainfo
  state clearly that Whale is an unofficial fork of KDE Dolphin and not affiliated with KDE.
- Metainfo: decide what happens to `src/org.kde.dolphin.appdata.xml` (installed or not).
  Complete `src/io.github.arkarit.whale.metainfo.xml`: description, screenshots (placeholders
  until RL8), `<releases>`, OARS content rating, URLs (homepage, bugtracker).
- Crash reporting: `KCrash` is initialized in `main.cpp`. Verify that DrKonqi never sends
  Whale crashes to bugs.kde.org (bug address is `WHALE_BUG_URL`). If it would, disable
  automatic reporting or route it to GitHub.
- Repo files: `CONTRIBUTING.md`, `SECURITY.md`, issue templates (bug / feature request) with
  guidance on what belongs to Whale and what belongs to Dolphin or KDE Frameworks.

#### Audit findings (2026-10-26)

Scope: 314 own commits on all 20 local and remote branches (everything not in `upstream/master`),
their messages, every added line including files deleted later, binary files, the current tree.

Clean:
- No credentials, tokens, keys or passwords anywhere in the history.
- No insults or inappropriate wording (only technical "dummy").
- Personal data in contents: only local paths under `/home/user/…` (neutral user name), today
  only in `CLAUDE.md`; nothing else (no real name, no private e-mail address).
- REUSE: 1019 of 1046 files compliant, no bad, missing or unused licenses.
- Crash reports: the bug address is `WHALE_BUG_URL` (GitHub issues), not bugs.kde.org; to be
  confirmed with a real crash (isolation test R10).
- `org.kde.dolphin.appdata.xml` is installed only with `WHALE_INSTALL_DOLPHIN_EXTRAS`.

Findings:
1. **Commit identity.** All 314 own commits have author and committer
   `User <user@ryzen-kubuntu.fritz.box>` (host name and home network domain; GitHub links them to
   no account). Changing it needs a history rewrite (all own hashes change, force push, hash
   references in `.privatedoc`, issues and memories need updating). Decision D4 applies:
   user decision.
2. **REUSE: 27 files without license information:** 23 `.privatedoc/*.md`, `AGENTS.md`,
   `CLAUDE.md`, `run-orcinus.sh`, `global-install-orcinus.sh`. Scripts: SPDX header
   (GPL-2.0-or-later). Documents: license to choose (e.g. CC-BY-SA-4.0 like many KDE docs, or
   CC0-1.0), via a `REUSE.toml` annotation.
3. **Icon.** The Orcinus icon is derived from Dolphin's (same Breeze folder, the dolphin replaced
   by an orca) and lives in Dolphin's file names `src/icons/*org.kde.dolphin*` and `logo.png`.
   Its license LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL and Dolphin's authors must stay;
   add the copyright line for the change ("2026 Orcinus contributors"). Optional: rename the
   files to the app ID (an upstream icon change would then no longer merge into them).
4. **No affiliation disclaimer.** README, metainfo and About dialog do not say that Orcinus is an
   unofficial fork not affiliated with or endorsed by KDE. README's "Contributing" section is
   upstream's text (KDE Invent, bugs.kde.org, "do not use the Issues tab") and contradicts
   Orcinus' issue tracker.
5. **Metainfo incomplete:** no `<releases>`, screenshots, OARS details (empty tag is fine for a
   file manager), the summary says "personal".
6. **Missing repo files:** `CONTRIBUTING.md`, `SECURITY.md`, issue templates.
7. **`CLAUDE.md`/`AGENTS.md`:** absolute local paths (`/home/user/…`); otherwise nothing that must
   not be public. They describe the local setup (build directories, private prefix).
8. **Helper scripts:** `global-install-orcinus.sh` assumes Debian/Ubuntu (`dpkg`) and
   `x86_64-linux-gnu`, `run-orcinus.sh` the local `../build-whale`; document them as developer
   helpers for that setup.
9. **`.privatedoc`:** 21 of 23 documents are German, about 95,000 words in total; the largest are
   the plans of view plugins (23k), multi-toolbar (16k) and location bar (11k). Which ones are
   obsolete and what gets translated is a user decision.
10. Upstream's CI files (`.gitlab-ci.yml`, `.kde-ci.yml`, `.flatpak-manifest.json`) stay for
    upstream merges; they do nothing on GitHub.

### RL2: String freeze and translation

- Declare a string freeze on master. From here on, string changes only for real fixes and only
  with an explicit note in the commit message.
- Extract the Whale-specific strings (`src/Messages.sh`), check the context comments
  (`i18nc`), check that nothing user-visible is hard-coded.
- Translate in one batch (German at least; other languages: decide here, e.g. machine
  translation with review, or "only German, rest falls back to Dolphin's translations").
- Decide the long-term translation workflow (e.g. Weblate on hosted.weblate.org, or PRs
  only). Must be documented in `CONTRIBUTING.md`.
- Verify that the translations built from `dolphin.po` plus the Whale strings work in a
  running Whale in at least German and one RTL language.

### RL3: Release engineering

- Version scheme: own (e.g. 1.0.0) or tied to Dolphin's release version. Record the Dolphin
  base version in the About dialog or `--version` either way.
- `CHANGELOG.md` and release notes. Source: `whale-ux-diary.md`, the plan documents,
  `docs/orcinus-vs-dolphin.md`.
- CI on GitHub Actions: build, ctest (offscreen), clang-format, REUSE, XML/YAML/JSON lint.
  Known pre-existing failure: `DolphinMainWindowTest::testAccessibilityTree` under offscreen
  (fails on upstream too); mark it as expected or skip it in CI.
- Release-build hardening: `WHALE_DEV_BUILD=OFF`, dev MCP off, KUserFeedback off. Add a
  check (CMake identity test or CI job) that a release configuration contains none of them.
- Release process: tag, source tarball, checksums, optional signing. Write it down as a
  short checklist so later releases follow the same path.
- Upstream strategy after release: how often upstream is merged, whether releases get a
  stable branch, and how upstream merges are tested before they reach a release.
- FileManager1: decide whether the first release offers the opt-in from
  `whale-rename-plan.md` section 6.1 or leaves "Show in folder" with Dolphin.

### RL4: Test containers

- Containers (Podman/Docker) for the most common distros with KDE Frameworks 6. Candidates:
  Kubuntu/Ubuntu LTS + current, Debian testing, Fedora KDE, openSUSE Tumbleweed, Arch,
  KDE neon. Final list: decide here, based on which KF6/Qt6 versions they ship.
- Each container: build dependencies, a build of Whale, the test suite offscreen.
- Check the minimum versions: Whale lowers the `KF6BalooWidgets` minimum to 25.12.3
  (`CMakeLists.txt`); `libmpv` is optional (`WHALE_WITH_MPV`); the system
  `libdolphinvcs.so.6` and VCS plugins are a runtime dependency. Note per distro what is
  available and what does not build.
- The containers are reused for packaging (RL5) and the distro tests (RL6).

### RL5: Packaging

- Choose the formats. Candidates: Flatpak (Flathub), AUR (`orcinus` / `orcinus-git`), `.deb` for
  Ubuntu/Debian (PPA or release asset), `.rpm` (Copr / OBS), AppImage. Decide which ones the
  first release ships.
- Dependencies: libmpv2 runtime, Baloo; `dolphin-plugins` only as a recommendation (VCS
  emblems). The VCS library is bundled privately (B2); list
  `src/views/versioncontrol/kversioncontrolplugin.*` (LGPL-2.0-only) in each package's copyright
  file. Flatpak: also build `dolphin-plugins` into the bundle, as Dolphin's Flatpak does.
- Coexistence with Dolphin must hold in every package: no shared files, own config, own D-Bus
  name, no change to MIME defaults on install.
- Uninstall and upgrade path: what stays behind (config, styles, state), what happens to the
  user's Dolphin settings (nothing).
- The `global-install-orcinus.sh` collision check is a good model for a package file check.

### RL6: Automated tests across distros

- In each container: build, ctest offscreen, install the package, start Whale headless,
  basic e2e via the dev MCP (dev builds only) or a smoke test on the release build (starts,
  opens a folder, quits cleanly).
- On a fresh user account: first start, default settings, Dolphin still works, Dolphin's
  config untouched, default file manager unchanged.
- Translation check: start with `LANGUAGE=de` and verify the German strings show up.
- Keep the scripts in the repo so CI (RL3) can run them later.

### RL7: Beta/RC and fixes

- Beta: a small group of testers, a feedback channel (GitHub Discussions or issues with a
  label), a defined test period.
- Go/no-go criteria, defined before the beta starts. Proposal: no known crash, no data loss,
  no open bug labeled `release-blocker`, all distros from RL4 build and pass RL6.
- Fixes go to master, a new RC follows when needed. The fix phase ends with the final RC.

### RL8: Website and documentation

- Do this after the string freeze and the final UI, so screenshots don't have to be redone.
- Website (GitHub Pages or similar): what Whale is, the relationship to Dolphin, features with
  screenshots, install instructions per distro/package, links to issues and docs.
- Fill `docs/orcinus-vs-dolphin.md` (outline exists). Decide whether the DocBook supplement to
  the Dolphin handbook is part of the first release or comes later.
- README rework: short pitch, screenshots, install, build from source, contributing.
- Final screenshots also go into the metainfo (RL1).

### RL9: Making the repo public

- Final check: RL1 audit still valid for commits since then, CI green, release tag set,
  packages built, website ready.
- Switch the GitHub repo to public. Set description, topics, website link.
- Review the open issues (#1–#5 and later) and decide which ones stay open publicly.

### RL10: Announcement

- Inform the Dolphin maintainers before the public announcement, out of courtesy and to offer
  features for upstream.
- Channels: decide here (e.g. KDE Discuss, r/kde, r/linux, Mastodon, Planet KDE needs a KDE
  account and is probably not the right place, Linux news sites).
- The announcement text links the website, the release notes and the packages, and states the
  relationship to KDE Dolphin clearly.
- Be prepared for the first days: watch issues, answer quickly, a fast bugfix release if needed.

## Open questions (decided in the respective steps)

- Languages of the first translation batch (RL2).
- Translation workflow after release (RL2).
- Version scheme (RL3).
- FileManager1 opt-in in the first release (RL3).
- Distro list (RL4) and package formats (RL5).
- Announcement channels (RL10).
