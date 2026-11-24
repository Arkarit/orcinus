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
matches Dolphin's behavior. As a rule, a new behavior that would change something existing
users rely on is off by default, and their settings and toolbars are kept; additions are on.
New users start with Orcinus' own defaults, see [section 11](#11-defaults-for-new-users).

Settings are under *Settings → Configure Orcinus…*; this document names the page and the
option, e.g. *View → Details → Dragging*.

## 1. Installing next to Dolphin

Orcinus installs and runs next to Dolphin without touching it:

- Its own application ID (`io.github.arkarit.orcinus`), executable (`orcinus`), D-Bus names,
  icon and task bar entry. Starting `dolphin` opens Dolphin, starting `orcinus` opens Orcinus.
- Its own configuration (`orcinusrc` and friends). Orcinus does not read `dolphinrc`; it
  starts with Dolphin's defaults. Toolbars, shortcuts and sessions are separate as well.
- Shared with Dolphin and all other KDE applications, as intended: places, trash, bookmarks
  (`kfile/bookmarks.xml` when it exists), service menus, "Create New" templates, Baloo, the
  thumbnail cache, and the display style saved for each folder.
- Dolphin's version control plugins (from `dolphin-plugins`), its service menus and its
  handbook are used when they are installed; Orcinus does not need a Dolphin package.
- "Show in folder" from browsers and other applications (`org.freedesktop.FileManager1`)
  stays with Dolphin in this release. Orcinus can be made the default file manager for
  opening folders in *System Settings → Default Applications → File Manager*.
- Crash reports go to Orcinus' issue tracker, never to bugs.kde.org.

## 2. Toolbars

### 2.1 A toolbar for every file view

Every file view (each half of a split view) has a *File View Toolbar* of its own, above
the view. Its buttons act only on that view and show that view's state: Back and Forward
(with their history menus), Up, Reload, the location bar, view mode, zoom, sorting,
previews, hidden files, filter and search. Clicking a button makes its view the active one,
so the next keyboard shortcut goes to the same view. Shortcuts themselves stay global and
act on the active view, as in Dolphin.

- *Settings → Configure Toolbars…* offers the File View Toolbar next to the Main Toolbar.
  One layout is shared by all file view toolbars.
- *View → Show File View Toolbars* shows or hides all of them, in all windows and tabs;
  their context menu has *Hide File View Toolbars*.
- New users get a short Main Toolbar (profiles, menu bar, menu button), because the
  navigation buttons are in the file view toolbars. Existing toolbars are kept as they are.

### 2.2 A location bar for every file view

Each view has its own location bar, in its file view toolbar (default position after
Up). When the view is narrow, the location bar moves into a second row instead of
disappearing into the toolbar's overflow menu; all views of a tab do this together, so
their file lists start at the same height. Hiding the file view toolbars hides the location
bars too. The location bar can be moved or removed like any toolbar element.

### 2.3 Split and search buttons

- A `>>` button at the right end of the toolbar of the only view opens the split view. With
  two views, a `<<` button at the end of the right view's toolbar closes the right view,
  whatever "close split view" is set to. Right-clicking `<<` offers *Split View To Tabs*
  and *Pop out Right View* (into a window of its own).
- The search button of a file view toolbar opens the search bar of that view.

### 2.4 The menu bar as a toolbar element

The menu bar can be placed in a window toolbar as the element *Menu Bar* (*Configure
Toolbars*; in the Main Toolbar of new users). It shows all menu titles side by side and turns into a
menu button when space runs out. It is independent of the classic menu bar, which *Ctrl+M*
still shows and hides.

### 2.5 Bar density

*Interface → Status & Location bars → Bar density* makes the bars around the file views
lower: the window toolbars, the file view toolbars with their location bars, the filter bar
and the status bar. Dialogs, menus and the views themselves do not change.

- *Normal*: Dolphin's look (default for existing users; *Defaults* restores it).
- *Compact*: smaller margins and buttons; text fields keep their height and frame (default
  for new users, see section 11). The two bars at the top take about 78 instead of 94 pixels.
- *Very compact*: also lower text fields and combo boxes, drawn without a frame; about 61
  pixels for the two top bars, a status bar of 20 instead of 30 pixels. Thin lines separate
  the file view from the file view toolbar and the filter bar.

The change applies at once. Toolbar icon sizes are set as before, per toolbar in its context
menu.

## 3. Split view

### 3.1 Each view keeps its own display style

With *View → General → Display style: Use common display style for all folders* (Dolphin's
default), Dolphin keeps one display style (view mode, sorting, hidden files, previews,
columns) for all folders *and both views*: the view changed last decides what the other one
shows in the next folder, and after a restart both look the same.

In Orcinus each view keeps its own display style when it goes to another folder. Places
with a display style of their own (trash, search, recent files, downloads) still show it,
and leaving them gives the view its own style back. A change is remembered for the view's
side (left or right) and is what new windows, tabs and split views start with. A view moved
to a tab (*Split View To Tabs*) or popped out to a window takes its style along.

With *Remember display style for each folder*, nothing changes compared to Dolphin.

### 3.2 Split view in new windows and tabs

*Interface → Folders & Tabs → Split new windows and tabs*: *As last used* (default),
*Always* or *Never*. This replaces Dolphin's checkbox "Open new windows in split view mode"
(when it was on, the setting becomes *Always*).

### 3.3 Linked views

The chain-link button at the left end of the right view's toolbar links the two views:

- Both go to the same folder, and the right view takes the location of the active one when
  linking.
- Selecting an item in one view selects it in the other. If the other view cannot show the
  item where it is (for example because its folder is not expanded there), it goes into the
  item's folder and back again when the selection returns.
- While linked, more than one item can only be selected inside one folder: *Ctrl+click* in
  another folder is refused, *Shift+click* and the selection rectangle stay in the folder,
  *Select All* selects the folder of the current item. A short message says so.
- Only local folders can be linked. Going to FTP, SMB, the trash or search results unlinks
  the views with a message.

The link belongs to the tab and is off after a restart (profiles can switch it on, see 5).
A typical use: the Cushion Treemap on the left, the file list on the right.

## 4. Views

### 4.1 View plugins

Besides Icons, Compact, Details and Columns (4.7), Orcinus can show a folder with other views
provided by plugins. Orcinus 0.1 comes with one, the Cushion Treemap. All views are listed together in
the View menu and in the view mode button of the file view toolbar; with more than five,
they move into a *View Modes* submenu. Options a view does not support stay visible but
disabled.

The view chosen for a location is remembered for that location only (not for its
subfolders), and in the history and the session. The display style saved for the folder is
not touched, so Dolphin is not affected.

### 4.2 Cushion Treemap

Shows how the space below the location is used: every file is a rectangle sized by its
size, folders are groups of rectangles. Hidden files count, because they take space too.

- Colors by *File Types* or by *Top Folders*; *Names* writes the names into the cells.
- Click selects, double click or *Enter* opens a file or goes into a folder. The arrow keys
  move between neighbors and levels. Mouse wheel and `+`/`−` zoom inside the treemap without
  changing the location.
- The selection works with the normal actions (copy, cut, trash, delete, rename,
  properties). The context menu adds *Show in Folder* and *Show in Parent Folder*.
- Many small entries are merged into one cell ("…"); clicking it selects their folder.
- Trash, delete and cut always ask and list the items, even when KDE's confirmations are
  off, because a deep selection is not necessarily visible.
- Only local folders, and the scan stays on one file system.

### 4.3 Show flat (no folders)

*View → Show flat (no folders)* lists the files of all subfolders of the location in one
list, without the folders, with a *Relative Path* column available in Details. Sorting,
filter, selection and previews work as usual. The setting is remembered per location.

The treemap and the flat view show items of many folders, so nothing is created in them:
*Create New*, *Paste*, *Duplicate* and dropping are disabled and say why. Moving, deleting,
renaming and opening work.

### 4.4 Expandable folders in the Compact view

The Compact view can expand folders like the Details view (*View → Compact → Folders:
Expandable*, on by default), with arrows and no tree lines.

In Compact, Details and the Folders panel, *Alt+click* on an arrow or `*` expands or
collapses a whole branch; `+` and `-` expand and collapse one level. Expanding a whole
branch never follows symbolic links and stops after 10,000 items.

### 4.5 Colored backgrounds and names for expanded folders

In the Compact view an expanded folder can wrap into the next column, and it is unclear
where its contents start and end. Orcinus colors the contents of expanded folders by depth,
so a subfolder reads as one block; colors repeat for deeper levels. Where there is room, the
names of the expanded folders are written vertically in the indentation next to their
contents.

The Details view does the same in the Name column; there the folder names stay at the top
of the view while scrolling through a long folder. Details' tree lines are replaced by these
colors.

*View → Highlighting → Expanded Folders* has a setting for each of the two views:
*Off*, *Printer paper*, *Grey*, *Pastel (Subtle)*, *Pastel (Vivid)* (default), *Custom* (2 to
10 colors of your own), and for Details *Classic (tree lines)*, which brings back Dolphin's
look. The presets adapt to light and dark color schemes. The folder names can be switched
off and their opacity set.

### 4.6 Information density

Dolphin leaves 2 pixels above and below every item. *View → Icons / Compact / Details /
Columns → Density* sets this space for each view mode:

- *Normal*: 2 pixels, Dolphin's look (default for existing users; *Defaults* restores it).
- *Compact*: 1 pixel (default for new users, see section 11).
- *Very compact*: no space; with 16 pixel icons and the usual system font a row is about
  18 pixels high instead of 22.
- *Custom*: the space in pixels (0 to 10) and, in Compact, Details and Columns, the height of a text
  line in percent of the font's (50 to 150 %). Below 100 % letters may touch the next row.

In the Compact view the gap between the columns shrinks with the density as well (8, 6 and
4 pixels). The space to the left and right of icon and name does not change.

### 4.7 Columns view

The Columns view (Miller columns, *Ctrl+4*) comes from Dolphin's development version: every
folder of the path is a column of its own, and selecting a folder opens it in a new column to
the right. Orcinus offers it as a fourth view mode next to Icons, Compact and Details. On top
of Dolphin's Columns view:

- It is listed with the view plugins (4.1), may be put on the file view toolbar and is part
  of profiles (5), including its zoom and density (4.6).
- With one display style for all folders, each view keeps its own mode (3.1): a Columns view
  stays a Columns view when it moves to another folder.
- Linked views (3.3) work with it: the other view follows the folder opened last, and the
  selection of the other view is shown in the column of that folder, without opening
  anything.
- The built-in viewer (6) moves the selection in the column it was opened from.
- *Show flat* (4.3) is not offered: each column lists one folder.

## 5. Profiles

A profile switches the whole look of a window at once, for example "one view, Compact, no
toolbars" for daily work and "one view, large icons, previews, Information panel" for
pictures.

- *View → Profiles → Save as Profile…* saves the current window as a profile. Applying one is
  a click in *View → Profiles* or its keyboard shortcut.
- A profile can set: split view, the display style and the view of each side, linked views,
  zoom, the information density (4.6), the bar density (2.5), the filter bar of each side,
  menu bar, toolbars, file view toolbars, status bar, panels and the window size (size or
  maximized; not the position, which applications cannot set on Wayland). Elements without a
  check mark are left as they are, so a profile can also change only a few things.
- *Save Profile "…"* updates the profile applied last with the current state of the window.
- The *Profiles* settings page lists the profiles: order, name, shortcut, the elements and
  their values, *New from Window*, *Capture Again* and *Apply to Window*.
- An applied profile is also what new windows, tabs and split views start with.
- *Startup profile* on the *Profiles* settings page: every new window opens with this
  profile. It is applied on top of what Orcinus remembers, including the folders and tabs
  restored with *Remember open folders and tabs*, so it overrides the remembered look. The
  window size of a profile is applied only then; switching profiles in an open window never
  resizes it. Views popped out of a split view keep their own look.
- New users get seven profiles, applied from the main toolbar: *Compact* (applied on the first
  start), *Dual Compact*, *Details*, *Dual Details*, *Images Only* (large icons, filtered to
  image files, no panels), *Locked Cushion Map* (Details and the Cushion Treemap, linked) and
  *AI Workbench* (Details between the AI Canvas panel on the left and the AI Assistant panel on
  the right). They are created once; profiles the user removes do not come back.
- While the file view toolbars are hidden, the profiles are also in the menu button and in
  the context menu of the view background, so there is always a way back.

The display styles in a profile are only applied with a common display style for all
folders (see 3.1).

## 6. Built-in image and video viewer

Opening an image (and, if Orcinus was built with libmpv, a video) shows it in a full-screen
viewer instead of an external application. Whatever is shown in the viewer is the current
and selected item in the view, so *Esc* leaves you exactly at the file you were looking at.

- Browsing: mouse wheel, arrow keys, *Page Up*/*Page Down*, *Backspace*, *Home*/*End*, or a
  click on the left or right edge of the screen. The viewer goes through the displayable
  files of the folder in the view's order, with its sorting and filter. If several files
  were selected when opening, only through those.
- Images: *Ctrl+wheel* or `+`/`-` zoom (up to 3200 %), `1` shows 100 %, `0` fits the screen,
  the middle mouse button pans. EXIF rotation and color profiles are respected, animated
  GIF and WebP play.
- Videos: *Space* or a click plays and pauses, *Left*/*Right* seek 5 seconds, `M` mutes;
  controls appear while the mouse moves.
- *Shift+double-click* (or *Shift+Enter*) skips the viewer and opens the file in its
  application.
- The *Viewer* settings page selects the file types the viewer opens (images; videos with
  libmpv) and has options for wrapping around at the end, enlarging small images, smoothing
  when zoomed in, and video playback, sound and looping.

## 7. File name highlighting

File names can be colored, bold or italic by rules, like colored `ls` output in a terminal
(*View → Highlighting → File Names*, on for new users).

- A rule matches executable files, symbolic links, MIME types (`image/*`, `text/x-python`)
  or name patterns (`*.bak`, case-insensitive by default), and applies to files, folders or
  both.
- Rules are applied from top to bottom; for each of color, bold and italic the first rule
  that sets it wins.
- Colors can follow the color scheme or be chosen freely; chosen colors stay readable.
- "Executable" means an executable bit on file systems where it means something (not on FAT,
  exFAT or NTFS) or a `.exe` file.
- Default rules: executable files, symbolic links (italic, as in Dolphin) and media files;
  *Defaults* restores them.

## 8. Filter bar

- *Keep Filter When Changing Folders* is on by default and remembered (in Dolphin it is off
  and the filter is cleared on every folder change). Closing the filter bar clears the text.
- A new button chooses what the filter applies to: *Files* (default; folders stay shown, so
  `*.jpg` no longer hides all folders), *Folders* or *Files and Folders* (Dolphin's behavior).
- In *Glob Pattern* mode, spaces, commas and semicolons separate several patterns; an item is
  shown if it matches one of them (`*.jpg *.png`). For a literal space, use `?`.

## 9. AI support

Orcinus can talk to an AI agent that runs on your computer, such as Claude Code, Codex or
OpenCode with a local model. You write in a panel; the agent sees the folder you have open and
what you selected, and it can open folders, select files and operate Orcinus for you. Orcinus
does not call any AI service itself: login, model, billing and the agent's own tools stay with
the agent. Nothing is set up by default and nothing leaves the computer before you add an agent.
How to set one up is described in [Setting up AI agents](ai-agents.md).

**Before you start.** An agent works with your rights: it can read, change and delete your
files, run programs and, if you allow it, ask for administrator rights. Orcinus has no sandbox
of its own. The first time, a dialog explains this; if you are new to AI agents, try them in a
virtual machine first.

### 9.1 The AI Assistant panel

*View → Panels → AI Assistant* (F8). At the top you choose the agent, start a new conversation,
open a saved one and reach the settings (*Settings → Configure Orcinus… → AI Support*).

- **Modes and permissions.** Every agent starts in its most restrictive mode (e.g. *Plan* or
  *Read-only*); the *Mode* box under the agent's name loosens it, and the agent starts in the
  mode chosen there next time. When the agent wants to do something its mode does not
  allow, it asks in the conversation, and you answer there. The panel opens on its own when a
  question waits.
- **What the agent sees.** With each message the agent gets the folder of the active view, the
  selected items and the folder of the other split view; the line under your message says what
  was sent. Files dropped on the input, copied files and pasted images go along as chips.
- **What the agent does in Orcinus.** It can open folders, select files, switch the view mode,
  split the view, change a number of settings and profiles, and run most actions of the menus.
  Each of these shows up as a line in the conversation (the *Actions* box hides them). It cannot
  change its own AI settings: it can only suggest a new agent, which you add yourself.
- **Commands.** A message starting with `/` is a command of the agent and goes to it alone (e.g.
  Claude's `/usage` with its 5-hour and weekly limits, or Codex' `/status`). Typing `/` lists
  Orcinus' and the agent's commands.
- **Usage.** The line under the agent's name shows the model and how full the context is; its
  tooltip lists the tokens of the conversation and, if the agent reports it, the cost (with a
  subscription only what the same use of the API would cost).
- **Suggestions.** A new conversation offers some requests to start with (rename files, check
  the system, report a problem with Orcinus, …). They are put into the input for you to
  complete; nothing is sent before you press Enter.
- **Administrator rights.** If allowed (*AI Support → Agents → Administrator rights*, on by
  default), the agent may run single commands as administrator through `pkexec`; the system asks
  for your password every time.

### 9.2 Conversations and memory

- **Saved conversations** (*AI Support → Memory → Conversations*, on, kept 90 days): the *Saved
  Conversations* button lists them; an agent that supports it continues a saved conversation
  where it left off. The same menu exports the conversation shown as PDF, Markdown or text.
- **Language**: agents answer in the language of Orcinus, or in the one set under *Talk with
  agents in*.
- **Memory** (*AI Support → Memory*): short notes the agents get in every conversation, for all
  folders or for one folder and its subfolders. *Manual* (default for new users): the agent
  proposes a note and you keep or discard it; *Automatic*: it keeps notes itself; *Off*. Write a
  note yourself with `/remember …` (for the folder shown) or `/remember global …`. The notes are
  the same for every agent and separate from the agents' own memories.
- **System prompt** (*AI Support → System Prompt*): what Orcinus tells every agent at the start;
  you can replace it with your own text.

### 9.3 Local models

Agents such as OpenCode can work with a model on your own graphics card instead of a cloud
service. Orcinus finds the models of **LM Studio** and **Strata**: *AI Support → Agents → Add →
Local Model (LM Studio)…* (or *(Strata)*) creates an agent for one of them. Orcinus starts the
model server when a conversation needs it and unloads the model when the conversation ends or
the panel stays hidden, so it does not keep the graphics memory; the button next to the agent
loads or unloads it by hand.

### 9.4 The AI Canvas and tools

*View → Panels → AI Canvas* is where the agent shows its work: images and videos it made, files
it found, sounds, texts and before/after comparisons. Clicking an item shows the file in the
view.

On request the agent also builds small **tools** for the canvas: a window of its own with
buttons, lists and previews (Qt Quick), optionally with a Python program behind it, e.g. "go
through the photos of this folder and let me keep or reject each one". A tool opens as a tab of
the canvas; *Keep on Canvas* keeps it for the next start. The tools button lists your tools and
offers example tools (*Add Example Tool*, e.g. *Photo Triage*). Tools run with your rights like
the agent; the program of a tool you have not kept starts only after you confirm it.

## 10. Behavior options

### 10.1 Dragging from anywhere in a Details row

*View → Details → Dragging: Drag files and folders from anywhere on the row* (off by
default). In Dolphin, dragging from the empty part of an unselected row starts a selection
rectangle. With the option, it drags the item; *Shift* plus drag there starts a rectangle.

### 10.2 Always follow symbolic links to their target

*Interface → Folders & Tabs → Always follow symbolic links to their target* (off by
default). A folder opened through a symbolic link is shown at its real location, so the
location bar shows the target path instead of the path of the link.

### 10.3 Running scripts in a terminal

*Interface → Confirmations → When opening an executable file* has, besides Dolphin's
*Always ask* (default), *Open in application* and *Run script*, four more choices: *Run
script in terminal*, *… and close*, *Run script in minimized terminal* and *… and close*.
The terminal starts in the script's folder and stays open on errors; a minimized terminal
comes to the front when the script fails.

*Shift+double-click* (or *Shift+Enter*) on a script always runs it in a terminal that stays
open, whatever the setting.

## 11. Defaults for new users

A new user (no Orcinus settings yet) starts with these differences from Dolphin's defaults:

- Main Toolbar: *Profiles*, the *Menu Bar* element and the menu button (2.1, 2.4).
- File View Toolbar: Back, Forward, view settings, Up, the location bar, filter and search.
- The profiles of section 5, starting with *Compact*.
- File name highlighting is on (7).
- The filter bar is shown in new windows, the status bar spans the full width and has the
  zoom slider; Compact view previews are 16 pixels.
- The information density is *Compact* in all four view modes (4.6), and so is the bar
  density (2.5).

## 12. Smaller differences

- *Help → About Orcinus* names the Dolphin version Orcinus is based on; its *Authors* tab lists
  Orcinus' authors first, then Dolphin's developers, each with its project.
- The Information panel also works with older versions of Baloo Widgets.
- *File → Recent Folders* lists the folders visited last, in all windows, and goes back to one
  when it is chosen; *Clear List* at its end empties it. The same menu is available as a
  toolbar button (*Configure Toolbars…*). Search results are not listed.
- Files copied or moved from FAT, NTFS or exFAT are no longer all executable: these file systems
  have no executable bit, and their mount options often mark every file as executable. After the
  copy, Orcinus removes the bit again, except for real programs (ELF binaries and scripts that
  start with `#!`). This covers copy, cut and paste, drag and drop and *Copy to/Move to Other
  View*; the *Copy To/Move To* submenus of the context menu are run by KIO and are not covered.

## 13. Known limitations

- The handbook and the help links describe Dolphin; the Orcinus features are only
  described here.
- Orcinus' own strings are translated into German, French, Spanish, Italian, Brazilian
  Portuguese, Polish, Russian, Simplified Chinese and Japanese; apart from German these are
  machine translations that have not been reviewed yet. Everything else uses Dolphin's
  translations.
- Orcinus' settings are not available in System Settings.
- "Show in folder" from other applications opens Dolphin (see 1).
