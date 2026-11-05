# RL: First public release (2026-10-25)

Whale is feature-complete for the first release (master 87b1388d3, full suite 99/99 green).
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
| D5 | Version of the first release? (2026-10-29) | **0.1.0, an alpha.** The user still sees rough edges (startup layout glitch, Styles data and naming, a mass-rename wish) that are no blockers for an alpha but need a decision before beta or 1.0. The B3 stability promises apply from **1.0**; in 0.x settings are still migrated where possible, action names and the plugin API may change. Follow-ups (user decisions 2026-10-29): D1 applies only to the next release, so features may come between releases in 0.x; no beta, but a release candidate before 1.0 (RL7); package formats for the alpha see RL5. |

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
   **Revised in RL3 (2026-10-26, user decision):** KF6 runs `kconf_update` from `kded6`, i.e. in
   Plasma sessions at login and when new `.upd` files appear; on other desktops it may never
   run. All of upstream's current `.upd` files (2024-12 to 2026-06) migrate older Dolphin formats
   that Orcinus never had, so nothing is ported for 1.0. Two paths instead of one: upstream
   migrations that arrive with a merge are ported as `orcinus_*.upd` (target `orcinusrc`,
   renamed helper programs; step of the merge checklist); Orcinus' own migrations are code run
   at start-up (like `WhaleSplitViewStartup::migrate`), which works on every desktop.
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
7. RL7 Release candidate and fixes (before 1.0; the 0.1.0 alpha ships without one)
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

#### Done (2026-10-26)

- REUSE compliant (1046/1046); own documents CC-BY-SA-4.0; icon copyright of the change added.
- `CLAUDE.md` paths relative; helper scripts described as developer tools.
- Unofficial-fork disclaimer in README, About dialog and metainfo; `CONTRIBUTING.md`,
  `SECURITY.md`, issue templates.
- `.privatedoc` sorted: plans, UX diary and idea list condensed into `design-notes.md` (ideas
  filed as issues #6–#12), dev MCP notes moved into its README, architecture notes translated;
  everything in English.
- Commit identity: all 323 own commits rewritten to `Arkarit <12444486+Arkarit@users.noreply.github.com>`
  with `git filter-repo --mailmap … --refs <branches> ^refs/remotes/upstream/master` (only own
  commits: a full run strips the SSH signatures of 46 upstream commits and changes the upstream
  hashes). Hash references in commit messages, documents, issues and memories remapped; all
  branches except `master` deleted locally and on GitHub; force-pushed. Backup:
  `../orcinus-before-identity-rewrite-2026-10-26.bundle`.
- Open for later steps: metainfo `<releases>` (RL3), screenshots (RL8), DrKonqi check (R10).
- **For RL9:** GitHub keeps the old, now unreachable commits (with the old identity) fetchable by
  hash until it garbage-collects them. Ask GitHub support to remove unreachable objects before
  making the repository public (recreating the repository would lose the issues).

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

#### Done (2026-10-26)

- **String freeze on master from 2026-10-26:** user-visible strings change only for real fixes,
  and such a commit says so in its message ("String change: …").
- 296 strings exist only in Orcinus (295 own + one upstream-style kcfg label); everything else
  comes from Dolphin's catalogs. Extraction and merging: `po-orcinus/update-catalogs.sh`
  (`extractrc.py` replaces KDE's extractrc, which is not installed here). Context comments
  checked (7 strings without context are kcfg labels and upstream-style messages); no
  hard-coded user-visible strings found.
- Languages (user decision): German plus French, Spanish, Italian, Brazilian Portuguese,
  Polish, Russian, Simplified Chinese, Japanese — all 296 strings, translated by the AI model
  with Dolphin's terminology per language; not marked fuzzy (msgfmt would drop them), the file
  headers say they are unreviewed machine translations. German waits for the maintainer's
  review.
- Workflow (user decision): pull requests now, Weblate (hosted.weblate.org) once the repository
  is public (RL9); documented in `CONTRIBUTING.md`.
- Verified: `LANGUAGE=de orcinus --help`; offscreen screenshots of a split view with the treemap
  in German (own strings translated) and Arabic (right-to-left layout mirrored correctly, own
  strings in English as there is no Arabic catalog).

### RL3: Release engineering

- Version scheme: own (e.g. 1.0.0) or tied to Dolphin's release version. Record the Dolphin
  base version in the About dialog or `--version` either way.
- `CHANGELOG.md` and release notes. Source: `design-notes.md`,
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
  "Default file manager and Show in folder" in `design-notes.md` or leaves "Show in folder" with Dolphin.

#### Done (2026-10-26)

- Version scheme (user decision): Orcinus' own semantic version, `WHALE_VERSION` = 1.0.0 (changed
  to 0.1.0 on 2026-10-29, see D5); the
  About dialog adds "Based on Dolphin <RELEASE_SERVICE_VERSION>". Dolphin's version still
  versions the internal libraries.
- Upstream strategy (user decision): follow Dolphin's `master`, merge every two to four weeks and
  before every release, no stable branches, hotfix branches from tags only when needed;
  documented in `docs/releasing.md` together with the release checklist.
- FileManager1 (user decision): not in 1.0; "Show in folder" stays with Dolphin.
- Migration path for `orcinusrc`: see B3 (revised).
- `cmake/WhaleReleaseCheck.cmake` rejects development builds, the dev MCP, telemetry, a
  non-optimized build type and binaries with the automation; run by the global install script
  and in CI.
- CI on GitHub Actions (`.github/workflows/ci.yml`): Arch Linux container (build, tests as a
  normal user with fonts, Breeze and thumbnailers, release check, formatting of changed lines)
  and a lint job (REUSE, XML, YAML, JSON, AppStream). Green since run 40325940554; two tests
  are excluded until issue #14 (selection timing, a tint pixel with newer Breeze).
- `CHANGELOG.md` with the 1.0.0 section (now 0.1.0, D5). The metainfo `<release>` entry is added at release time
  (checklist step 5).

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

#### Done (2026-10-27)

- Distro list (user decision), all built as Podman images in `ci/containers/` (see its
  README): Ubuntu 26.04 LTS (KF 6.24 / Qt 6.10, at the minimum versions), Ubuntu 26.10,
  Debian testing, KDE neon, Fedora 45, openSUSE Tumbleweed, Arch. Not supported: Debian 13
  (KF 6.13). Two layers: a generic `kde-dev` image and an `orcinus-dev` image on top;
  `containers.sh images|test|shell`. Orcinus builds with all optional features on all seven.
- Test results: only issue #14 fails everywhere; Ubuntu 26.10 additionally three
  `KColorButton` crashes of KF 6.29 on the offscreen platform (accepted as a limit of the
  test environment); Fedora 45 skips the script tests (Qt 6.11 + shared-mime-info 2.5: shell
  scripts are no `text/plain`, so KIO does not run them as scripts either).
- Fixed on the way (dba73d309): the lost Ctrl+X in `whalecommandroutingtest` (window
  activation offscreen), a longer wait in `whaletreecachetest`, all tests run with
  `LANGUAGE=en_US` (22 failed under a German desktop), an XDG menu in the apt images for
  KService 6.24.
- Open: `folderspaneltest` fails rarely in full parallel runs (Ubuntu 26.04 once, locally
  once); not reproducible alone, with 32 parallel copies or with churn in `/tmp`.
- GitHub CI green for dba73d309 (run 40492029542). Note: `actions/checkout@v4` runs on
  Node.js 20, which GitHub deprecates; update when the CI is touched next.

#### Format decision (2026-10-29, user decision)

- Alpha 0.1.0: a GitHub pre-release with the source tarball (tag `v0.1.0`), AUR (`orcinus`,
  `orcinus-git`) and an Ubuntu PPA for 26.04 and 26.10 (the maintainer's own system is
  26.04). `global-install-orcinus.sh` stays the way to build from source.
- Flatpak only for the RC/1.0: in the sandbox a file manager loses Baloo search, kio-admin,
  the terminal panel with the host shell and the host's service menus and VCS plugins, which
  would give the alpha a worse first impression; Flathub's manual review (days to weeks) also
  starts then. Fedora (Copr) and openSUSE (OBS) when there is demand; the `debian/` directory
  of the PPA can be reused for Debian testing.

### RL5: Packaging

- Formats for the alpha: GitHub source tarball, AUR, Ubuntu PPA (decided, see the end of
  RL4). Flatpak follows for the RC.
- Dependencies: libmpv2 runtime, Baloo; `dolphin-plugins` only as a recommendation (VCS
  emblems). The VCS library is bundled privately (B2); list
  `src/views/versioncontrol/kversioncontrolplugin.*` (LGPL-2.0-only) in each package's copyright
  file. Flatpak: also build `dolphin-plugins` into the bundle, as Dolphin's Flatpak does.
- Coexistence with Dolphin must hold in every package: no shared files, own config, own D-Bus
  name, no change to MIME defaults on install.
- Uninstall and upgrade path: what stays behind (config, styles, state), what happens to the
  user's Dolphin settings (nothing).
- The `global-install-orcinus.sh` collision check is a good model for a package file check.

State (2026-11-05):

- AUR registration is temporarily closed (spam wave), so the PPA comes first; the AUR recipes
  follow and are uploaded once registration reopens.
- Launchpad: user `arkarit`, PPAs `ppa:arkarit/orcinus` and `ppa:arkarit/orcinus-testing`
  (both empty). An upload publishes the source, so the first upload waits for RL9 (user decision).
- Ubuntu packaging done: `packaging/debian/` (single binary package `orcinus`, DEP-5 copyright
  including the LGPL-2.0-only VCS library) and `packaging/ubuntu-ppa.sh` (source / test /
  upload). Version scheme `X.Y.Z-0ppaN~SERIES1`, snapshots `X.Y.Z~gitDATE.HASH`. Both series
  (resolute 26.04, stonking 26.10) build in clean Ubuntu containers with only the
  Build-Depends, install next to Dolphin without file conflicts, have only relative RUNPATHs and
  start (`orcinus --version`). Remaining lintian: no manual page, no watch file (accepted).
- Open: Maintainer address in debian/control (currently the GitHub noreply address);
  `liborcinusprivate` still lives in the public library directory (could move to
  `lib/<arch>/orcinus` like the other private libraries).

### RL6: Automated tests across distros

- In each container: build, ctest offscreen, install the package, start Whale headless,
  basic e2e via the dev MCP (dev builds only) or a smoke test on the release build (starts,
  opens a folder, quits cleanly).
- On a fresh user account: first start, default settings, Dolphin still works, Dolphin's
  config untouched, default file manager unchanged.
- Translation check: start with `LANGUAGE=de` and verify the German strings show up.
- Keep the scripts in the repo so CI (RL3) can run them later.

### RL7: Release candidate and fixes

No beta (user decision 2026-10-29); the alpha 0.1.0 itself ships without an RC, the RC phase
belongs to the way to 1.0. Feedback channel (GitHub Discussions or issues with a label).

- Go/no-go criteria, defined before the RC. Proposal: no known crash, no data loss,
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

- Distro list (RL4, decided) and package formats (RL5, decided for the alpha).
- Announcement channels (RL10).
