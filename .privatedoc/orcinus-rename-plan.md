# OR: Rename Whale → Orcinus (2026-10-25)

Release blocker B1 (`whale-release-plan.md`): "Whale" collides with `org.kde.whale` and other
projects; the user chose **Orcinus** (trademark search done, see B1). The icon stays.

Basis: master aff29389e. All findings below were checked in the source and in the install
manifest of `../build-whale-system`.

**Goal:** everything a user, packager or plugin author sees, and everything Whale stores on disk
or on the bus, says Orcinus. Dolphin keeps working in parallel exactly as before (the rules of
`whale-rename-plan.md` still apply).

**Non-goals:** no rename of internal code names (decision N1). No change to what is shared with
Dolphin (system `libdolphinvcs`, `dolphin/vcs`, `[Dolphin]` view properties, handbook).

## Decisions

Recommendations in bold; **confirmed by the user on 2026-10-25.**

| # | Question | Recommendation | Alternative |
|---|---|---|---|
| N1 | Internal code names (`src/whale/`, classes `Whale…`, CMake variables `WHALE_*`, test names, action names `whale_*`, dev/test env vars `WHALE_*`) | **Keep.** About 7,500 occurrences in `src/whale/` alone; renaming them is pure churn. Dolphin itself keeps internal names that differ from products (target `dolphin` builds `whale`). `CONTRIBUTING.md` explains "Whale was the working title". | One mechanical rename commit (sed + `git mv`), now or after 1.0 |
| N2 | Action names `whale_*` (stored in the user's toolbar and shortcut files) | **Keep** (part of N1). They are invisible, and keeping them makes the config migration a plain copy. | Rename to `orcinus_*`, plus migration of every kxmlgui file |
| N3 | View plugin externals: namespace `whale/views`, metadata keys `X-Whale-*`, library `libwhaleviewapi.so`, plugin file `whale_treemap.so` | **Rename now** to `orcinus/views`, `X-Orcinus-*`, `liborcinusviewapi.so`, `orcinus_treemap`. Packagers see the paths, plugin authors the keys; after 1.0 renaming them would break plugins. Headers are not installed yet, so the C++ class names stay (N1). | Keep (cheaper now, permanent "whale" in installed paths) |
| N4 | Logging category `org.kde.whale` | **`io.github.arkarit.orcinus`** (same as the app ID; `org.kde.*` suggests a KDE project), file `orcinus.categories`. | `orcinus` |
| N5 | SPDX `2026 Whale contributors` (240 files) | **Change to `2026 Orcinus contributors`** in one mechanical commit. Same people, so legally only a name change; consistent for a public repo. | Keep (historically correct, but confusing) |
| N6 | Local directories `../build-whale`, `../build-whale-system`, `install-whale`, checkout dir | **Keep.** Local only; renaming forces a reconfigure and touches CLAUDE.md, memories and scripts for no user benefit. | Rename together with the scripts |
| N7 | `.privatedoc/whale-*.md` | **Keep the file names** (history, cross-references in commits). The RL1 translation pass changes the product name in the text where it describes the current state; "Whale" stays where it describes the past. | Rename all to `orcinus-*` |
| N8 | Migration of existing user data | **One-off script, not shipped** (`.privatedoc/orcinus-migrate-user.sh`). The user is the only Whale user; in-app migration code would stay in the product forever for nobody. | Migration at first start in Orcinus |

## Inventory: what changes

### Central constants (`CMakeLists.txt`)

| Constant | Today | New |
|---|---|---|
| `WHALE_EXECUTABLE` | `whale` | `orcinus` |
| `WHALE_COMPONENT` | `whale` | `orcinus` (→ `orcinusrc` default config, `orcinusstaterc`, `~/.local/share/orcinus`, KCrash, session names, D-Bus object path) |
| `WHALE_APP_ID` | `io.github.arkarit.whale` | `io.github.arkarit.orcinus` (desktop file, icon, metainfo, D-Bus service names, Wayland app_id) |
| `WHALE_DISPLAY_NAME` | `Whale` | `Orcinus` (dev build: `Orcinus-Dev`) |
| `WHALE_BUG_URL` / `WHALE_HOMEPAGE` | `github.com/Arkarit/whale…` | `github.com/Arkarit/orcinus…` |
| `WHALE_ORG_DOMAIN` | `arkarit.github.io` | unchanged |

### Hard-coded names that should derive from the constants

| Where | Today | Change |
|---|---|---|
| `src/CMakeLists.txt:14` | `TRANSLATION_DOMAIN="whale"` | from `WHALE_COMPONENT` |
| `src/CMakeLists.txt:194–195` | category `org.kde.whale`, description "Whale" | N4, from constants |
| `src/CMakeLists.txt:260` | `OUTPUT_NAME whaleprivate` | `${WHALE_COMPONENT}private` → `liborcinusprivate.so.6` |
| `CMakeLists.txt` translation loop | `whale.mo`, `po-whale/<lang>/whale.po` | `${WHALE_COMPONENT}.mo`; directory `po-orcinus/<lang>/orcinus.po` |
| `CMakeLists.txt` completions | `completions/whale.zsh.in` → `_whale` | `orcinus.zsh.in` → `_orcinus`, `#compdef orcinus` |
| `plasma-whale.service.in` | | `plasma-orcinus.service.in` (only with FileManager1 ON) |
| `src/io.github.arkarit.whale.metainfo.xml` | file name and content | `io.github.arkarit.orcinus.metainfo.xml`; id, name, binary, URLs; "based on KDE Dolphin" stays |
| `src/dolphin.qrc:2` | prefix `/kxmlgui5/whale` | `/kxmlgui5/orcinus` (must match the component) |

### Config and state files (hard-coded in source)

| Today | New | Where |
|---|---|---|
| `whalerc` | `orcinusrc` | `<kcfgfile>` in 11 `.kcfg` files (same list as `whale-rename-plan.md` step 3) plus code that opens it explicitly |
| `whalestylesrc` | `orcinusstylesrc` | `whale/styles/whalestylestore.cpp:77` |
| `whaleviewpreferencesrc` (state) | `orcinusviewpreferencesrc` | `whale/host/whaleviewpreferences.cpp:26` |

Where the code opens one of them by name, derive it from the component
(`QCoreApplication::applicationName() + "rc"`, …) so a later rename touches only CMake.

### Runtime identities in code

| Where | Today | New |
|---|---|---|
| `src/global.h:47` | command-line option `--whale-view-style` (pop out) | `--view-style`. Hidden from `--help` if it is not already |
| dev automation | `io.github.arkarit.whale.DevAutomation`, `/whale/DevAutomation` | from app ID / component |
| `src/whale/devmcp/adapter/whale_dev_mcp.py:600` | bus-name prefix `io.github.arkarit.whale` | `io.github.arkarit.orcinus` (adapter file name stays, N1) |
| `whaleviewregistry.cpp:26`, `plugins/treemap/CMakeLists.txt:15` | namespace `whale/views` | `orcinus/views` (N3) |
| `whaleviewapiversion.h.in:21`, `whaleviewactions.cpp:590`, `whale_treemap.json.in` | `X-Whale-ViewApiVersion`, `X-Whale-RequiresLocal`, `X-Whale-Capabilities` | `X-Orcinus-*` (N3) |
| `src/whale/CMakeLists.txt:25` | `libwhaleviewapi.so.10` | `OUTPUT_NAME orcinusviewapi` (target name stays) |
| treemap plugin | `whale_treemap` (also the plugin ID stored in view preferences and styles) | `orcinus_treemap`; the migration script rewrites the stored IDs |

### User-visible strings

- 17 `i18n` calls contain "Whale" (`main.cpp` `--help` texts, `dolphinmainwindow.cpp` tooltip and
  credit line "Whale Fork: %1", `dolphinviewcontainer.cpp`, settings pages, viewer and styles
  pages, servicemenuinstaller). Replace with "Orcinus" literally, like upstream writes "Dolphin";
  translators then see the product name.
- Credit line: "Orcinus: %1" or "Fork: %1" instead of "Whale Fork: %1".
- `po-whale/de/whale.po`: rename and update the msgids. The real translation batch is RL2 anyway.
- `settings/viewmodes/expansiontintwidget.cpp:327`: the example folder "whale" in the tint
  preview → "orcinus".

### Repository files

- `README.md`, `AGENTS.md`, `CLAUDE.md`, `docs/orcinus-vs-dolphin.md` → `docs/orcinus-vs-dolphin.md`.
- Scripts: `run-whale.sh` → `run-orcinus.sh`, `global-install-whale.sh` →
  `global-install-orcinus.sh` (content: binary, manifest names; build dirs stay, N6).
- SPDX headers (N5).
- `REUSE.toml`: check annotations that mention Whale.

### Guards

- `cmake/WhaleIdentityTest.cmake` (file name stays, N1): additionally forbid
  `io\.github\.arkarit\.whale`, `"whale[a-z]*rc"`, `/kxmlgui5/whale`, `X-Whale-`, `"whale/views"` and
  "Whale" inside `i18n…(` string literals.
- The DESTDIR collision check of the install script: no installed path contains `whale`.

## What stays

- Everything in N1/N2/N6/N7.
- Everything shared with Dolphin (`whale-rename-plan.md` section 3.4).
- History: no rewrite. Commits, issues and old `.privatedoc` texts keep saying Whale.

## Steps (one commit each; build both configs and run the full suite after each)

### OR1: Installed file names (no change of the runtime identity)
Names that only affect installed files: `liborcinusprivate`, `liborcinusviewapi` (output name,
part of N3), logging categories (N4, including `org.kde.whale.views` in `src/whale/CMakeLists.txt`),
completions, plasma service template, metainfo file name, `po-orcinus`, `.mo` file name and the
translation domain. The domain comes from one place: `TRANSLATION_DOMAIN` (CMake, derived from
`WHALE_COMPONENT`); `main.cpp` (`whaleTranslationDomain`) and the ~30 tests with
`setApplicationDomain({"whale"})` use that macro instead of a literal.
Test: full suite; DESTDIR install of `../build-whale-system` → manifest contains no `whale`.

Done (2026-10-26) with one deviation: the installed names are derived from `WHALE_COMPONENT`,
`WHALE_EXECUTABLE` and `WHALE_APP_ID` (`${WHALE_COMPONENT}private`, `${WHALE_COMPONENT}viewapi`,
`${WHALE_COMPONENT}.categories`/`.mo`, `_${WHALE_EXECUTABLE}`, category `${WHALE_APP_ID}`), so
they still say `whale` after OR1 and switch together with the constants in OR2. Only the source
files were renamed (`po-orcinus/de/orcinus.po`, `completions/orcinus.zsh.in`,
`plasma-orcinus.service.in`). The metainfo file name moves to OR2 because it is installed as
`${WHALE_APP_ID}.metainfo.xml`. The manifest check moves to OR2 accordingly.

### OR2: Runtime identity (constants plus everything hard-coded that depends on them)
Found while starting OR1: the constants (`WHALE_APP_ID`, `WHALE_COMPONENT`, …) drive the bus
names, the D-Bus object path, the default config file and the kxmlgui component at runtime, and
many tests hard-code those values (`/whale/Whale_1`, `:/kxmlgui5/whale/…`, `whalerc`,
`whalestaterc`, `whaleviewpreferencesrc`, `io.github.arkarit.whale-<pid>` in the e2e tests).
Switching the constants alone would leave the suite red, so constants and the dependent names
change in one commit:
- central constants, display name (`Orcinus`, dev build `Orcinus-Dev`), URLs;
- config/state file names (`.kcfg` files, `whalestylesrc`, `whaleviewpreferencesrc`), derived
  from the component where code opens them by name;
- qrc prefix `/kxmlgui5/orcinus` (the rc file names inside stay, N2);
- pop-out option `--view-style`; dev automation bus names; MCP adapter prefix;
- unit tests and e2e tests that hard-code any of these;
- identity test extended.
Internal group names inside the config files (e.g. `WhaleToolbars`) stay (N1/N2), so the
migration remains a plain copy.
Test: full suite including the dev MCP e2e tests.

Done (2026-10-26, 53ced168a), including the metainfo file moved here from OR1. `--view-style`
was already hidden from `--help`.

### OR3: View plugin externals (N3)
Namespace, metadata keys, library output name, treemap plugin ID; test plugins in
`src/whale/tests/plugins/` follow.
Test: plugin loading tests including the wrong-API-version test.

Done (2026-10-26, 78625b59c). Deviation: the test plugins keep their IDs (`whale_hello`,
`whale_testlist`, …; internal, N1) and only move to the new namespace and keys. Plugins are sorted
by ID, so `orcinus_treemap` now comes before them in the View Modes menu; one test expectation
changed accordingly.

### OR4: Visible strings and the German catalog
17 i18n strings, credit line, `po-orcinus/de/orcinus.po` msgids.
Test: `LANGUAGE=de` start of the dev build, `--help` output.

Done (2026-10-26, 2eb86dbd4). Credit line is "Orcinus: %1". The German catalog had no msgids
naming the product (the "Whale …" strings were untranslated before as well), so only its header
changed; the translations follow in RL2. Checked with `LANGUAGE=de orcinus --help` (offscreen)
after installing into the private prefix.

### OR5: Repository and docs
README, AGENTS.md, CLAUDE.md, docs file, scripts, REUSE. SPDX headers as a separate mechanical
commit (N5), so the content commits stay readable.

Done (2026-10-26, 92ca0f213 content, 761e14f08 SPDX). `CONTRIBUTING.md` does not exist; the note
"Whale was the working title" is in README.md instead. `REUSE.toml` had nothing to change.

### OR6: GitHub
Rename the repo `Arkarit/whale` → `Arkarit/orcinus` (GitHub redirects old URLs), update the
`origin` URL, update the issue texts of #1–#5 where they name the product. Done by the user or
by me with `gh` after confirmation, because it is outward-facing.

### OR7: Migration of the user's installation (N8)
`.privatedoc/orcinus-migrate-user.sh`, run once by the user with Whale closed:
- copy (not move, so a rollback is possible): `~/.config/whalerc` → `orcinusrc`,
  `whalestylesrc` → `orcinusstylesrc`, `~/.local/state/whalestaterc` → `orcinusstaterc`,
  `whaleviewpreferencesrc` → `orcinusviewpreferencesrc`, `~/.local/share/whale/` → `orcinus/`,
  `~/.local/share/kxmlgui5/whale/` → `orcinus/`;
- rewrite stored plugin IDs `whale_treemap` → `orcinus_treemap` in the copied files;
- `~/.config/mimeapps.list`: `inode/directory` → `io.github.arkarit.orcinus.desktop`;
- Plasma: pinned task-manager launchers and a possible Meta+E shortcut that point to
  `io.github.arkarit.whale.desktop` (`plasma-org.kde.plasma.desktop-appletsrc`,
  `kglobalshortcutsrc`): the script lists them, the user changes them in the UI;
- dry-run mode that only prints what it would do.

System install: uninstall the old Whale with its `install_manifest.txt`, install Orcinus with
`global-install-orcinus.sh`, check that `dolphin` still starts and `dpkg -V dolphin` is clean.

Also update the Claude Code side: memories, the `whale-dev` MCP server entry (name can stay;
the bus prefix changed in OR2).

Script written and tested on a copy of the user's files (2026-10-26); not run on the real home.

Found during OR1–OR3: the DESTDIR test installs overwrote `../build-whale-system/install_manifest.txt`,
so it no longer lists the installed Whale. The list was reconstructed from the OR1 staging (all
104 files present in `/usr`) plus four unowned leftovers `libwhaleviewapi.so.8*`/`.9*` from older
installs: `../build-whale-system/whale-uninstall-manifest.txt`. Uninstall with
`sudo xargs rm < ../build-whale-system/whale-uninstall-manifest.txt`, then
`sudo rmdir /usr/lib/x86_64-linux-gnu/qt6/plugins/whale/views /usr/lib/x86_64-linux-gnu/qt6/plugins/whale`.

### OR8: Acceptance
- Isolation tests R1–R15 from `whale-rename-plan.md` section 7.3 with the new names, plus:
  Orcinus and a leftover Whale must not talk to each other (different bus names).
- After the migration: styles, toolbars, view preferences and shortcuts of the user are all there.
- `grep -ri whale` over the installed files: no hits, except texts that deliberately say
  "formerly Whale".

## Risks

| Risk | Mitigation |
|---|---|
| Missed hard-coded name → Orcinus reads or writes a `whale*` file | Identity test (OR2), `strace -e openat` check during OR8 |
| Dev MCP e2e tests break on bus names | OR2 changes the adapter in the same commit |
| Upstream merges | The changes are in Whale-only code or in the lines already changed for Whale; no new conflict areas |
| The user's daily Whale breaks during the switch | The migration copies; the old config stays until the user deletes it |
