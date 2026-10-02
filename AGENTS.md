# AGENTS.md

## Project

This repository is a fork of KDE Dolphin that is being developed into **Whale**. The immediate goal
is a personal file manager that can coexist with the system Dolphin; later publication remains an
option. Preserve KDE authorship, licensing, and upstream history.

`origin` is the Whale fork. `upstream` is KDE Dolphin. Keep Whale-specific changes focused and easy
to replay across upstream merges.

## Read before changing code

- `CLAUDE.md` contains the practical build, test, and architecture summary. Its instructions apply
  to every coding agent despite the filename.
- `.privatedoc/dolphin-architektur.md` is the detailed source map and extension-point analysis.
- `.privatedoc/whale-rename-plan.md` is the product-rename and collision-avoidance plan. Its section
  9 decisions were confirmed on 2026-09-29 and phase 1 has been implemented.

Treat `.privatedoc/` as internal project documentation unless the user explicitly asks to publish
it.

## Build and test safety

- The established out-of-tree build is `../build-dolphin`; do not reconfigure it casually.
- Its private install prefix is `../install-dolphin`; never install development artifacts into the
  system prefix.
- For the Whale rename, configure a separate `../build-whale` and use a separate
  `../install-whale` prefix.
- A focused build does not refresh installed runtime resources. After changing icons, desktop
  files, translations, or similar data, run `cmake --install ../build-whale` into the private
  `../install-whale` prefix before visual testing; never install them into the system prefix.
- Run `../build-whale/bin/whale --new-window` when a fresh development window is wanted. Whale and
  Dolphin now have distinct D-Bus identities; without the option, Whale may reuse its own instance.
- Prefer focused builds/tests while iterating, then run the relevant broader test set. Tests use
  QTest and are registered in `src/tests/CMakeLists.txt`.
- New source files need SPDX headers. Keep KDE clang-format and CI conventions intact.

## Fork boundaries

- This is a product rename, not an internal rewrite. Unless a feature requires otherwise, retain
  Dolphin class names, source filenames, namespaces, CMake target names, translation sources, and
  upstream-facing structure.
- Keep narrowly scoped features and fixes Dolphin-compatible whenever practical. Structure them as
  small, self-contained changes that can be replayed onto a plain Dolphin fork and proposed
  upstream as merge requests; isolate unavoidable Whale product integration from the transferable
  core change.
- Whale and the installed Dolphin must remain usable in parallel. Never introduce shared installed
  filenames, application identity, configuration, session state, or private-library SONAMEs.
- In particular, an installed Whale must use `libwhaleprivate`, never `libdolphinprivate`; an ABI
  collision can break the system Dolphin.
- Keep intentionally shared KDE resources shared: KIO infrastructure, places/bookmarks, service
  menus, Baloo data, thumbnail cache, trash, and (for phase 1) per-folder view properties.
- Avoid edits to `po/` and `doc/` merely for branding; the rename plan describes how to consume
  upstream translations without creating merge-heavy changes.
- Do not commit generated build output or modify unrelated user changes.

## Architecture landmarks

The main UI path is:

`DolphinMainWindow -> DolphinTabWidget -> DolphinTabPage -> DolphinViewContainer -> DolphinView`

The file view uses Dolphin's own `QGraphicsView`-based framework under `src/kitemviews/`, not Qt's
standard item views. Filesystem access is URL-based and goes through KIO.

For future extensibility, start at the active-view signal wiring in
`DolphinMainWindow::activeViewChanged()` and `connectViewSignals()`. Natural integration points are
panel construction, additional context-menu actions, action registration, item roles/decorations,
and information-panel preview dispatch. Consult the architecture document before introducing a new
plugin mechanism.
