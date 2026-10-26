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

### B3: What is stable after 1.0

The style format (`Version` key) and the view plugin API (`X-Whale-ViewApiVersion`) are already
versioned. To decide: whether the plugin API is meant for third parties and promised stable, or
declared internal/unstable for 1.0 (proposal); and how config key changes are migrated once
users have them.

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

- Choose the formats. Candidates: Flatpak (Flathub), AUR (`whale` / `whale-git`), `.deb` for
  Ubuntu/Debian (PPA or release asset), `.rpm` (Copr / OBS), AppImage. Decide which ones the
  first release ships.
- Dependencies: Dolphin's VCS library and plugins (Phase 1 reuses them), libmpv2 runtime,
  Baloo. For Flatpak these have to be bundled or the features degrade cleanly.
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
