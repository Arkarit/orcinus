# Orcinus wishlist

Feature wishes that are recorded but not planned yet. An entry here is no commitment; it gets a
plan (and milestone codes) only when it is picked up.

## Git integration (2026-10-29)

Dolphin's official Git plugin (`fileviewgitplugin` from `dolphin-plugins`) can do very little.
Wish: an own Orcinus Git plugin that covers the everyday basics in a TortoiseGit-like way. The
command line stays available for everything else.

In scope:

- Commit like TortoiseGit: a dialog with the changed files, check boxes for staging, diff, message.
- Pull, push.
- Merge.
- Branch handling: switch, create, delete.
- Settings per repository.

Not in scope, at least at first: revision graph, interactive rebase, submodules, bisect, patches.

Notes:

- TortoiseGit (GPLv2, MFC/Win32) serves as a feature catalogue only; its code cannot be ported.
  The official plugin is the starting point.
- The system `fileviewgitplugin.so` is loaded as well; two Git plugins would both match `.git`.
- Related bug, independent of the plugin: `VersionControlObserver` treats every folder below a
  directory that merely contains a `.git` entry as versioned (`QFile::exists` check in
  `searchPlugin()`, then `startsWith(m_localRepoRoot)` without a trailing `/` in
  `verifyDirectory()`). An empty `~/.git` makes the whole home folder look like a repository.

## Mass rename tool (2026-10-28)

A tool to rename many files at once. Noted with the alpha decision (release plan D5).
