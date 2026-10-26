<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Changelog

All notable changes to Orcinus. Orcinus is based on KDE Dolphin and follows its development;
changes that come from Dolphin itself are not listed here. Versions follow
[semantic versioning](https://semver.org/).

## 1.0.0 (unreleased)

The first release. Compared to Dolphin 26.11 (development state), Orcinus adds:

### Identity

- Installs and runs next to Dolphin: own application ID, executable, configuration, D-Bus names
  and translation domain. Places, trash, service menus and per-folder view properties stay
  shared with Dolphin. Does not need a Dolphin package; Dolphin's version control plugins are
  used when they are installed.
- "Show in folder" from other applications stays with Dolphin in this release.

### Toolbars

- A toolbar for every file view with its own Back, Forward, Up, Reload, view settings and more,
  acting only on its own view; configured in Configure Toolbars ("File View Toolbar").
- A location bar for every file view, in its toolbar; it wraps into a second row when the view
  is narrow.
- A shorter main toolbar for new users. Existing toolbars are kept as they are.
- The menu bar can be placed in a toolbar and turns into a menu button when space runs out.

### Split view

- Each view keeps its own view mode and settings, also after a restart; the start-up split can be
  "as last used", "always" or "never".
- Linked views: both views go to the same folder and select the same items.
- Quick buttons to open and close the second view; the right view can be moved to a tab or
  window.

### Views

- View plugins, starting with the Cushion Treemap, which shows the space used below a folder.
- "Show flat (no folders)": the files of all subfolders in one list.
- Expandable folders in the Compact view, and Alt+click or `*` to expand a whole branch.
- Colored backgrounds and folder names for expanded folders in the Compact and Details views.

### Styles

- A style switches the whole look of a window at once: one or two views, view modes, toolbars,
  panels, filter bars and the status bar. Styles are saved, given shortcuts and managed on a
  settings page of their own.

### Viewer

- A built-in full-screen viewer for images and, with libmpv, videos: opens like any file, the
  selection in the view follows it, zoom, preloading, animated images, video controls.

### Highlighting

- File names can be colored and styled by rules: executable files, symbolic links, MIME types
  and name patterns, for files, folders or both.

### Smaller changes

- The filter keeps its text when changing folders by default, and can apply to files only,
  folders only, or both.
- Optional: drag files from anywhere in a Details row.
- Optional: always follow symbolic links to their target.
- Scripts can run in a terminal, also minimized.
- Translations of Orcinus' own strings in German (reviewed), French, Spanish, Italian, Brazilian
  Portuguese, Polish, Russian, Simplified Chinese and Japanese (machine translations, not yet
  reviewed); everything else uses Dolphin's translations.
