<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Orcinus compared to Dolphin

Orcinus is based on KDE Dolphin and follows its upstream development. Everything not
listed here behaves as described in the
[Dolphin Handbook](https://docs.kde.org/?application=dolphin), which Orcinus's
*Help → Handbook* opens as well.

Each section states what differs, where to find it, and whether the default
matches Dolphin's behavior.

> Status: outline. Sections marked *TODO* still need their text.

## 1. Application identity and coexistence

<!-- TODO: own app id (io.github.arkarit.orcinus), binary `orcinus`, own D-Bus name and
     configuration files, so Orcinus and Dolphin can be installed and run side by side;
     Orcinus does not take over "Show in folder" (org.freedesktop.FileManager1);
     settings are not shared with Dolphin; handbook, service menus and version
     control plugins are Dolphin's. -->

## 2. Toolbars and location bars

### 2.1 A toolbar for every file view
<!-- TODO: File View Toolbar per view, its action set, Configure Toolbars,
     slimmer main toolbar, showing/hiding, the way back to hidden toolbars
     without the menu bar, migration of existing toolbars. -->

### 2.2 A location bar for every file view
<!-- TODO: each view has its own location bar; it wraps into a second row
     instead of the overflow menu; Tab moves through each file view starting
     at its toolbar. -->

### 2.3 Split and search buttons
<!-- TODO: split button ">>" at the end of the toolbar of the only view, "<<"
     at the end of the right view's toolbar (right click: Split View To Tabs,
     Pop out Right View);
     search button for the File View Toolbar. -->

### 2.4 Each view keeps its own style
<!-- TODO: with "Use common display style for all folders", every view keeps
     its own style (view mode, sorting, hidden files, preview, columns) when it
     goes to another folder; Dolphin reads the common style again in every
     folder, so a view took what another view changed last. Places with a style
     of their own (trash, search, recent files, downloads) still show it, and
     leaving them gives the view its style back. A change is remembered for the
     view's side (left/right) and is what new windows, tabs and split views
     start with. The view left when the split view closes keeps its style; a
     view moved to a tab (Split View To Tabs) or popped out to a window takes
     its style along. Settings > Folders & Tabs: "Split new windows and tabs"
     as last used / always / never (replaces "Open new windows in split view
     mode"). "Split View To Tabs" keeps the other view in any "close split
     view" setting. With a style for each folder, nothing changes. -->

## 3. Views

### 3.1 View plugins and the view switcher
<!-- TODO: views beyond Icons/Compact/Details provided by plugins; view modes
     in one level of the View menu; reachable without the menu bar; the
     preferred view is remembered per location; shown view is kept in history
     and sessions. -->

### 3.2 Cushion Treemap
<!-- TODO: what it shows, navigation and zoom, context menu, selection works
     with the normal actions, rest cells, removal confirmation, nothing is
     created in views without a destination folder. -->

### 3.3 Files below the location ("Show flat (no folders)")
<!-- TODO: lists the files of all subfolders in one flat list; where to switch
     it on; interaction with other views. -->

### 3.4 Expandable folders in the Compact view
<!-- TODO: expansion arrows in Compact view (setting, default), no tree lines;
     Alt+click / "*" expands or collapses a whole branch (also in Details). -->

### 3.5 Colored backgrounds for expanded folders
<!-- TODO: in the Compact view, the contents of expanded folders get a
     background color by depth, so a subtree reads as one block even where it
     wraps into the next column; colors repeat for deeper levels. An expanded
     folder carries the color of its contents right of its arrow. Where there
     is room, the names of the expanded folders are written vertically in the
     indentation next to their contents, in every column again (option with
     an opacity slider, on by default). The Details view does the same in
     the Name column, with the row stripes running through the colors and
     the names sticking to the top of the view; Compact and Details have
     settings of their own (switch in the dialog). The tree lines of the
     Details view are gone; the preset "Classic (tree lines)" brings back
     Dolphin's look without colors and names. Settings >
     View > Highlighting > Expanded Folders: Off, Printer paper, Grey,
     Pastel (Subtle), Pastel (Vivid) (default), Custom (2 to 10 colors,
     add/remove, preview). Presets adapt to
     the color scheme, custom colors are used as chosen. Not in the Details
     view. -->

## 4. File name highlighting

<!-- TODO: Highlighting tab in the view settings; rules by type and file flags
     (e.g. executable) and by name patterns (case sensitivity); rules apply to
     files, folders or both; colors and font styles; the default rules;
     why an empty-pattern rule cannot be saved; performance note. The tab
     also holds the colors for expanded folders (see 3.5). -->

## 5. Behavior options

### 5.1 Dragging from anywhere in a Details row
<!-- TODO: setting in Details view settings, default off (Dolphin behavior),
     how click and drag are told apart, modifier behavior. -->

### 5.2 Always follow symbolic links to their target
<!-- TODO: setting in Interface → Folders & Tabs, default off; what changes in
     the location bar and context menu. -->

### 5.3 Built-in image and video viewer
<!-- TODO: opening images and videos shows them in a plain fullscreen viewer
     instead of an external application; changing the image moves the current
     item and selection in the view; multi-selection shows only the selected
     files; zoom and pan keys; video playback (libmpv); settings page.
     Shift+double-click (or Shift+Enter) skips the viewer and opens the file
     in its preferred application. -->

### 5.4 Running scripts in a terminal
<!-- TODO: executable scripts run in the default terminal (Settings, default
     off = KIO asks as in Dolphin): keep open, close on success, minimized
     variants raised when the script fails; the terminal starts in the
     script's folder. Shift+double-click (or Shift+Enter) always runs the
     script in a terminal that stays open, whatever the setting. -->

## 6. Smaller differences

<!-- TODO: About dialog credits Dolphin; Information panel available with
     older Baloo Widgets; "Orcinus-Dev" title for development builds (only
     relevant for contributors, maybe move to a developer section). -->

## 7. Known limitations

<!-- TODO: handbook and help links still describe Dolphin; translations of new
     features are incomplete; Orcinus-specific settings are not available in
     System Settings modules. -->
