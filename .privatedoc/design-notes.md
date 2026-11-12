# Orcinus design notes

What Orcinus does differently from Dolphin, and why: the decisions behind each feature, the
alternatives that were rejected, and where the code lives. This document condenses the step-by-step
plans and the UX diary written during development (September and October 2026, still under the
working title Whale). The original German plans remain in the Git history; the milestone codes
used in code comments and commit messages (UX-001, M4, TB2, LK6, V3, …) are kept as headings here,
so those references can still be looked up.

General rules that apply to every feature:

- **Defaults.** A new behavior that changes something existing users rely on is off by default,
  unless it only adds something; their settings and toolbars are kept. New users get Orcinus' own
  defaults (since 2026-11-10, listed in `docs/orcinus-vs-dolphin.md` section 10).
- **Non-intrusive.** Following Dolphin's development philosophy, features must not clutter menus,
  context menus or toolbars for users who do not need them.
- **Transferable core.** Narrow features and fixes are kept small, self-contained and free of
  Orcinus-only dependencies, so they can be replayed onto a plain Dolphin and offered upstream.
  Unavoidable product integration (own config file, settings pages) stays separate from that core.
- **Visible but disabled rather than hidden.** Options that do not apply in the current view stay
  in the menu, disabled, and keep their state (UX-004). Menus do not rebuild themselves when the
  view changes.

---

## UX-001: Dragging from the empty part of a Details row

**Problem.** In Details view a row is highlighted over its full width, but only icon and text are
a drag handle while the row is unselected: dragging from the empty part of an unselected row starts
a selection rectangle, from a selected row it drags the selection. The geometry depends on the
selection state (`selectionRectCore()` vs. `selectionRectFull()`), and `KItemListController::onPress()`
decides for the rubber band before the press selects the row.

**Decision.** Both expectations (drag the row / start a rectangle in a full view) are legitimate;
Windows Explorer and Total Commander resolve the same conflict differently, the latter by an option.
Orcinus keeps Dolphin's behavior as the default and adds the option "Drag files and folders from
anywhere on the row" (Configure → View → Details, next to "Open files and folders by clicking
anywhere on the row"). With it, dragging from anywhere on the row drags the item; Shift plus drag
there starts a rectangle; a Shift click without movement stays a range selection.

**Rejected.** Replacing Dolphin's behavior (takes the rubber band start area from users of full
views); coupling to "Highlight entire row" (look and gesture are separate properties); Shift as a
symmetric inverter in Dolphin mode (collides with Shift range selection and keeping the selection
during a rubber band); Ctrl/Alt/Meta (Ctrl toggles selection and changes the drop action, Alt/Meta
are taken by window managers); a new "Interaction" settings tab for a single option.

**Code.** `KItemListView::dragFromAnywhereOnRow` (off by default; the controller knows no settings),
`KItemListWidget::selectionRectContent()` (selection-independent content area, Details: icon and
text), `DetailsModeSettings::DragFromAnywhereOnRow`, mapped in `DolphinItemListView`. Test:
`KItemListControllerTest::testDragFromAnywhereOnRow()`. Incoming drops are unchanged. Upstream
candidate.

## UX-002: File name highlighting

**Problem.** File managers show all names in the same text color; executable files (X bit) cannot
be told apart without the Permissions column. Terminals have colored `ls` output for decades.

**Decision.** On for new users since the default setup of 2026-11-10 (15793aee9; first designed as
off by default, exactly Dolphin); a stored `Enabled=false` is kept. When on, an ordered,
editable list of rules styles
the file *name* only (color, bold, italic):

- One condition kind per rule (Executable, Symbolic link, MIME type, name pattern), any number of
  patterns of that kind (OR). No AND, no negation. Later: "Applies to: files and folders / folders /
  files" (`AppliesTo`).
- **Cascade per attribute:** rules are evaluated top to bottom; for each attribute (color, bold,
  italic) the first matching rule that sets it wins. Bold and italic are tri-state (unchanged / on /
  off). The role `NormalText` in a higher rule deliberately suppresses colors of lower rules.
- A color is a `KColorScheme` role or an explicit color; roles are resolved when painting and follow
  every color scheme. A central contrast correction keeps explicit colors readable.
- Priority: selection (palette) > VCS state with a meaning > rule color > Dolphin. Hidden files keep
  a dimmed variant of the rule color. Size, date and other columns keep Dolphin's colors.
- With highlighting on, the rule list is the only source of the name style: Dolphin's built-in
  italic for symbolic links becomes the default rule "Symbolic links → italic"; deleting the rule
  removes the italic.
- **Executable** = known Windows executable extension (only `.exe` for now, on every file system)
  OR a POSIX executable bit, but the bit only counts on file systems where it means something
  (`KFileSystemType`: not FAT, NTFS (also via ntfs-3g/fuseblk), exFAT). Directories never count.
- `image/*` means the canonical MIME group, exact types use `inherits()`. Name patterns are
  wildcards, case-insensitive by default (per rule).
- Default rules: Executable files, Symbolic links, Media files; "Defaults" restores them.
- Settings: an own tab "Highlighting" under Configure → View (`General | Content Display |
  Highlighting | Icons | Compact | Details`). Rules are stored as groups
  `[FileNameHighlighting][RuleN]` in `orcinusrc`, conditions as stable text.

**Rejected.** A fixed rule set (too inflexible for types); taking `LS_COLORS` as the basis
(terminal colors and attributes do not map to color schemes; an import stays possible); emblems or
a new column (emblem corners are taken by VCS and plugins; the point is "at a glance"); a separate
entry in the settings sidebar (too prominent for an optional display feature).

**Code.** `FileNameHighlightRule`, `FileNameHighlighter` (immutable per rule set, compiled
patterns), `KStandardItemListWidget::setNameTextColor()` (name-only color, separate from
`setTextColor()`), decision in `DolphinFileItemListWidget::refreshCache()`. The MIME type may switch
shortly after an item appears, like the icon, because the roles updater determines it from content.

## UX-003: About dialog with fork credits

The About tab shows a tagline ("A Dolphin. Just a big one." — an orca is the largest dolphin; always English, not translated),
"File Manager", the Dolphin version the code is based on (Orcinus has its own version since
RL3, `WHALE_VERSION`), then two separate blocks: Dolphin's copyright, repository and license, and the fork's
credit, homepage and license; since 2026-10-26 also the line that Orcinus is unofficial and not
affiliated with KDE. `KAboutApplicationDialog` merges short description, other text, copyright and
homepage into one rich-text label and adds one label per license, so Orcinus shows the dialog with
a copy of the `KAboutData` that puts everything (including both license links) into the short
description and hides the automatic license label (found by its link target `"0"`). The global
`KAboutData` stays plain, so `--help` prints no HTML. Accepted: the license links open gnu.org; if
KDE changes the dialog's layout, at worst a duplicate license line appears.

## UX-004: View modes in one level, options by capability

The View menus list Icons, Compact, Details and the plugin views as one list (plugins sorted by
plugin ID); exactly one entry is checked. Up to five modes appear directly, from six on all of them
go into one "View Modes" submenu — never only the plugins, never "File List → Icons". Grouping:
view modes | zoom, sort, group, additional information | "Show flat (no folders)" (formerly
"Include Subfolders"), previews, hidden files | restore/adjust. Whether an option applies is asked
from the active view's capabilities (`WhaleViewHost::supportsViewOption()`, capability
`HiddenFiles` added in view API 9), never by view ID. Unsupported options stay visible, disabled,
and keep their state (no automatic switching off). The menu bar is merged with KDE's
`ui_standards.rc`, which puts Zoom first, so the modes are inserted deterministically before
"Sort By" after every GUI rebuild.

**Rejected.** Plugins in an own submenu (forces a technical decision "file list or plugin?" before
the actual one); hiding unsupported options; making the treemap honor "Show Hidden Files" (it is
about space, hidden files count); letting Icons/Compact/Details write a view preference (O3: mode
changes inside the file list are not a view choice).

## UX-005: Nothing is created where no target folder is visible

Views that show items of many folders (flat view, treemap) are not read-only, but they create
nothing: Create New, New Folder/File, Paste, Duplicate and drops are disabled and explain why (also
when triggered by shortcut); copying or moving into the other half of a split view is refused when
that half is such a view. Allowed: trash, delete, cut, copy, rename (stays in its folder), Copy
Location, properties, open. The treemap always asks before trash, delete and cut and lists the
items, even when KDE's confirmations are off, because a deep selection there is not necessarily
visible; the flat view follows KDE's settings (every file is a visible row). Remainder cells of the
treemap ("…") are flat and always carry a folder icon, since a click selects that folder. The view
was renamed "Cushion Treemap".

**Rejected.** Creating in the location itself (invisible where it lands); fully read-only (the
treemap exists to clean up); hiding instead of disabling (UX-004).

## UX-006: Main toolbar and file view toolbars

New users (without an own copy of `dolphinui.rc`) get a slim main toolbar: location bar, split,
stash, search, hamburger menu; Back, Forward and the view settings are in each file view toolbar,
where they act. Existing users keep their main toolbar byte for byte; the file view toolbar is
added with its default, duplicate buttons are accepted. One switch for all file view toolbars:
"Show File View Toolbars" in the View menu, "Hide File View Toolbars" in their context menu; it
applies to all views, tabs and windows and persists. Configure Toolbars always offers the file view
toolbar, also while hidden. No notice to existing users.

**Rejected.** Rebuilding existing toolbars automatically (changes a deliberately designed bar);
visibility per pane, tab or view type; an entry in KDE's "Shown Toolbars" (lists only the window's
bars); a banner for existing users; a "Reset File View Toolbar" entry (KDE's Defaults resets both).

## Product identity: coexisting with Dolphin (Whale rename, 2026-09, and Orcinus rename, 2026-10)

The fork installs and runs next to the distribution's Dolphin without any collision in files, IDs,
D-Bus names, configuration or runtime behavior. The first rename (Dolphin → Whale) set up the
mechanisms; the second (Whale → Orcinus, `orcinus-rename-plan.md`) only changed the values.

Decisions (the first rename, still valid):

- **Own application ID** `io.github.arkarit.orcinus` (reverse domain of the maintainer; `org.kde.*`
  is reserved for KDE projects): desktop file, D-Bus names, Wayland app_id, icon, AppStream.
- **Component name** `orcinus`: `orcinusrc`, `orcinusstaterc`, `~/.local/share/orcinus`, KXMLGUI
  component, session names, D-Bus object path `/orcinus/Orcinus_1`. All derived from a few CMake
  constants (`WHALE_EXECUTABLE`, `WHALE_COMPONENT`, `WHALE_APP_ID`, `WHALE_DISPLAY_NAME`, URLs) in
  the top-level `CMakeLists.txt`, passed to C++ through `config-dolphin.h`.
- **Private library under an own name** (`liborcinusprivate`): the system has
  `libdolphinprivate.so.6` with a different ABI, and `/usr/local/lib/<arch>` comes before
  `/usr/lib/<arch>` in `ld.so.conf`; the same SONAME would make the system Dolphin load the fork's
  library and crash.
- **VCS:** Dolphin's VCS plugins (`dolphin/vcs`, from `dolphin-plugins`) are loaded with
  `qobject_cast` against the metaobject of exactly one `libdolphinvcs`; a renamed VCS library would
  reject every system plugin. Originally the system library was used; since 2026-10-26 (release
  blocker B2) a private copy under the same SONAME is installed in `lib/<arch>/orcinus/`, which
  the system plugins load against.
- **Not installed by default:** KPart, KCMs, context menu plugins, service menu installer, KNewStuff
  file, handbook, developer headers (`WHALE_INSTALL_DOLPHIN_EXTRAS=OFF`); Dolphin provides them.
- **Translations:** own domain; Dolphin's `po/*/dolphin.po` are compiled into the fork's `.mo`
  files by a CMake rule, merged with the fork's own strings (`po-orcinus/`); `po/` stays untouched
  for upstream l10n syncs.
- **`org.freedesktop.FileManager1`** ("Show in folder" from browsers) is not claimed by default
  (`WHALE_PROVIDE_FILEMANAGER1=OFF`), otherwise whoever starts first gets the calls.
- **Telemetry** (KUserFeedback with Dolphin's product ID) is disabled.
- **No global shortcut** (Meta+E) in the desktop file; users assign it.
- **Fresh configuration**, no automatic import of `dolphinrc` (a manual copy works).
- Crash reports go to the fork's issue tracker (`setBugAddress`), never to bugs.kde.org.

Runtime collisions that had to be fixed (all derived from the constants now): single-instance
search prefix, D-Bus object path, own service names, "Open in New Window" and detaching a tab
(started `dolphin`), config file name in the kcfg files, KXMLGUI component and qrc prefix (toolbar
and shortcut changes went to Dolphin's files), session management names, window icon, telemetry,
FileManager1 registration, translation domain, the `--sudo` hint.

**Deliberately shared with Dolphin** (KDE-wide, intended):

- `kdeglobals`, `kiorc`, `kservicemenurc` (enabling a service menu affects all KIO programs, as in
  Dolphin)
- KIO service menus, `kfile/bookmarks.xml`, places (`user-places.xbel`), trash
- Baloo, the thumbnail cache, "Create New" templates
- per-folder view properties (xattr `kde.fm.viewproperties#1` or `.directory` group `[Dolphin]`);
  the global view properties are separate automatically (`~/.local/share/orcinus/view_properties`)
- the internal drag MIME type `application/x-dolphin-blacklist-drop`
- the D-Bus interface name `org.kde.dolphin.MainWindow` (an interface, not a bus name, so no
  collision; keeps the generated classes unchanged)
- `help:/dolphin/…` links and `X-DocPath` (Dolphin's handbook)

**Runtime isolation tests** (manual, with the real desktop; first done for Whale, repeated for
Orcinus in OR8). Before: save checksums of Dolphin's files
(`sha256sum ~/.config/dolphinrc ~/.local/state/dolphinstaterc ~/.local/share/kxmlgui5/dolphin/*`).

| # | Test | Expected |
|---|---|---|
| R1 | Dolphin runs, then `orcinus ~/Downloads` | A new Orcinus window; Dolphin gets no tab |
| R2 | Orcinus runs, then `dolphin ~` | Goes to Dolphin, not to Orcinus |
| R3 | Orcinus runs, then a second `orcinus ~/tmp` | Tab or window in the running Orcinus instance |
| R4 | `qdbus \| grep -i 'orcinus\|dolphin'` | `io.github.arkarit.orcinus-<pid>` and `org.kde.dolphin-<pid>` separate; object path under `/orcinus/…` |
| R5 | Orcinus: detach a tab, "Open in New Window", middle-click a folder | Only Orcinus windows appear |
| R6 | Orcinus: change a setting, a toolbar, a shortcut, move a panel, quit | Only Orcinus' files change; Dolphin's checksums unchanged |
| R7 | Task bar / window switcher | Own icon and group; pinning is independent |
| R8 | Help → About Orcinus | Name Orcinus; Dolphin's credits and copyright unchanged |
| R9 | `LANGUAGE=de orcinus --new-window` | German interface |
| R10 | `kill -SEGV $(pidof orcinus)` | DrKonqi names Orcinus and offers no report to bugs.kde.org |
| R11 | Enable the Git plugin in Orcinus' settings, open a repository | Git emblems and context menu in Orcinus; Dolphin's setting unchanged |
| R12 | Right-click a file | Entries such as "Move to New Folder" appear once (from Dolphin's package) |
| R13 | Information panel, previews, terminal panel, search, trash, "Act as Administrator" | Work as in Dolphin |
| R14 | Log out and in with Orcinus and Dolphin open | Both restored separately |
| R15 | Session file | Under `~/.config/session/orcinus_*`, not `dolphin_*` |

## View plugins (M0–M11, T1, T2, O3, F1; 2026-10-03 to 2026-10-07, API 10 on 2026-10-20)

**Goal.** Views other than Dolphin's file list (the first one: a cushion treemap) as binary
plugins, without rebuilding Dolphin's most churned files.

**Findings that shaped the architecture** (checked in the code):

- K1: `DolphinView` is a view *plus half a host*: it owns the listing (`KFileItemModel`/`KDirLister`),
  the selection, file operations, per-folder view properties, status text, writability, the VCS
  observer and tooltips. The clean host/plugin cut would run *inside* it.
- K2: the shell reaches the concrete `DolphinView` through `DolphinViewContainer::view()` at about 200
  places (45 of them pull the selection); these are the files upstream changes most.
- K3: the selection is index based, lives in the controller and only covers the loaded model.
- K6: Dolphin's history, tab and session formats must not be extended (upstream would conflict).
- K7: view modes are an enum persisted per folder in data shared with Dolphin (`[Dolphin]`), so the
  chosen plugin view is never written there.
- K9/K10: previews are tied to `KFileItemModel` and the visible range; there is no recursive tree
  scan.

**Architecture.** `DolphinView` stays in every container and remains the authoritative backend
(listing, filter, sorting, selection store, file operations, status bar); `view()` is always valid,
so the shell is unchanged. A plugin view is shown *instead of* it (stacked widget) while it keeps
running hidden.

- `WhaleViewHost` (one per container, internal) owns the stack, the active view and its states,
  does capability gating and turns focus into activation.
- `DolphinViewBridge` is the *only* class that touches Dolphin internals (model access through one
  accessor `DolphinView::itemListContainer()`); it may emit `DolphinView`'s *request* signals
  (context menu, item info, tabs) so plugin events use the existing wiring, never state signals.
- `DolphinViewAdapter` shows the `DolphinView` itself as a view ("file list", view ID `dolphin`).
- Public API in `liborcinusviewapi` (only Qt/KF types; a boundary lint keeps Dolphin and kitemviews
  types out): `WhaleView` (a `QObject` with `widget()`, so the adapter can return the existing
  widget), `WhaleViewPlugin`, `WhaleViewContext` (abstract facade: location, items in host order,
  selection, requests, services), `WhaleViewCapabilities`, `WhaleSelection`, `WhaleItemList`,
  services `WhalePreviewProvider`, `WhaleTreeScanner`, `WhaleFileOperations`.
- Plugins: KCoreAddons (`KPluginMetaData::findPlugins("orcinus/views")`,
  `KPluginFactory::instantiatePlugin<WhaleViewPlugin>`), the same mechanism as Dolphin's VCS
  plugins. The registry checks `X-Orcinus-ViewApiVersion` in the JSON before loading; the version
  must match exactly (internal API, see release blocker B3). Libraries are never unloaded.
- The old view instance is destroyed on a switch; expensive data lives in services with caches.

**Focus (M0 spike, M2).** Contrary to the Qt documentation, `setFocus()` on a hidden widget makes it
the focus widget in Qt 6.10 and activates the split half. Rule: the `DolphinView` stays enabled as
the shell's focus target, its interactive children are disabled, and its focus proxy points to the
plugin widget, so all eight `view()->setFocus()` calls of the shell reach the plugin unchanged.
Disabling the `DolphinView` itself broke those calls. F2 checks whether the `DolphinView` is the
visible view, not whether it is enabled.

**Cost of the hidden view (K12).** Measured: invisibility stops nothing; per folder change the work
equals a visible view (up to 500 previews and folder sizes), idle cost is zero. Accepted; pausing the
roles updater of an invisible view (M11) stays an optional upstream candidate.

**Selection.** Three sources: `file-list` (Dolphin's), `plugin-mirrored` (a plugin mirrors Dolphin's
selection by URL, M4; echo suppression per context), `plugin` (a view's own deep selection with
capability `RecursiveContent`, T1/T2). The selection survives a view switch because it never leaves
the `DolphinView`.

**Capability gating (M5).** Each gateable action gets its own gate; while a plugin view is visible,
actions the view does not support are disabled after Dolphin's action handler has updated them, and
Qt restores Dolphin's last wish when the gate opens. Without `InlineRename` rename uses
`KIO::RenameFileDialog` (an inline editor would open in the hidden view). The file list gates
nothing. Capabilities: `Selection`, `MultiSelection`, `HostOrder` (shows the host's order, so
sorting, filter and hidden files apply for free), `Zoom`, `Previews`, `HiddenFiles` (API 9),
`InlineRename`, `RecursiveContent`, `KeyboardNavigation`, `LinkedSelection` (API 10).

**Persistence (M6).** Own channels, never Dolphin's formats: a session key `WhaleViews` next to the
tab's `State` (`WHALESS\x01`), an envelope around Dolphin's history location state (`WHALEVS\x01`),
format version `WhalePersistence::FormatVersion` independent of the API. Unknown, disabled,
incompatible or unloadable plugins fall back to the file list.

**Previews and drag and drop (M7).** `WhalePreviewProvider` wraps `KIO::PreviewJob` with Dolphin's
preview plugins and KIO's shared thumbnail cache, with queueing, priorities and cancellation;
active only when Dolphin's "Show Previews" is on for the location. `WhaleFileOperations` builds
mime data (`KUrlMimeData`, portal export) and drops through `DragAndDropHelper::dropUrls`.

**Tree scanner and cache (M8a/b).** One recursion for everything: `WhaleTreeScanner` (fts in a worker
thread, `FTS_PHYSICAL`: symbolic links are leaves of size 0; `FTS_XDEV`: stays on one file system;
cancellable; immutable shared `WhaleTreeNode` snapshots). `WhaleTreeCache` per main window shares
scans (hit / join a running scan / miss), invalidated by inotify watches per folder (`KDirWatch`
costs the same and was slower in a prototype), with memory budgets and eviction. Measured: 100k
files scanned in about 185 ms on a warm SSD.

**Treemap (M9, UX-005).** Plugin `orcinus_treemap`, only public API: squarified layout (Bruls,
Huizing, van Wijk 2000), cushion rendering with `QPainter`, data from the tree scanner (hidden
entries count: it is about space). Single selection, view-local and URL based; click selects the
deepest element, arrows move between neighbors and levels, Enter/double click open the folder as a
location, wheel and +/- zoom inside the treemap without changing the location. No `Zoom` capability
(the host's zoom actions belong to the file list).

**Deep items (T1, T2).** The deep selection belongs to the view and is never forced into Dolphin's
listing (a selection override in `DolphinView::selectedItems()` was rejected: indexes are used for
mime data, inline rename and "select next after delete"). T1: `WhaleFileOperations` work on explicit
URLs; the treemap has its own context menu (`KFileItemActions` plus operations on the explicit
list, "Show in Folder"), followed by an automatic rescan. T2: command routing — global action →
active selection source → matching path (`WhaleCommandRouter`, central table
`WhaleViewActions::routedActions()`); Dolphin's slots ask the router first, about 20 one-line
changes in Dolphin files; the hidden Dolphin selection never wins over a plugin source. Not routed:
Compare, Show Target.

**Preferred view per location (O3).** Only the user's explicit view choice is stored, as a view ID
per location (`orcinusviewpreferencesrc [PreferredViews]`, hash of the cleaned location key), never
in `[Dolphin]` view properties or xattrs. No inheritance to subfolders. Priority: session/history
over preference. Modes inside the file list (Icons/Compact/Details) are not a view choice.

**Show flat (F1, "Include Subfolders" until UX-004).** A content scope of the file list, not a
view: the three basic modes show the local files of all subfolders (no folders), fed by the tree
scanner's projection into `KFileItemModel::setExternalItems()` (about 140 lines in the model), with a
new role "Relative Path". Filter, sorting, selection and previews stay Dolphin's. "Recursive may be
the source, never the target": drops and paste are refused and explained. Scope per location in
`[ContentScopes]`, in history and session as part of the file list's state.

**Test views.** `Hello` and `Test List` are test fixtures, built into a test plugin directory and
never installed.

## File view toolbars (TB0–TB6, 2026-10-08 to 2026-10-17)

**Goal.** Besides the window's main toolbar, every file view (each half of a split view) gets its
own toolbar that acts only on that view and shows that view's state.

**Configuration with KDE's own dialog.** `KEditToolBar` already shows a "Toolbar:" combo box as soon
as the XMLGUI documents of the factory contain more than one editable toolbar, and offers the
actions of the collection of the client that owns the selected toolbar. A second XMLGUI client for
the file view toolbar (`whalefileviewtoolbarui.rc`) therefore gives selection, order, separators,
spacers, icon/text changes and Apply/OK/Cancel across both toolbars without an own dialog. One
layout is shared by all file view toolbars. The main toolbar stays `mainToolBar` in
`dolphinui.rc`, so existing user files keep working. Accepted deviation: KDE's "Defaults" resets all
toolbars, not only the selected one. The prototype client is in the factory only while the dialog
is open (lifetime bound to `QDialog::finished`).

**Action targeting (the actual problem).** Nearly all Dolphin actions implicitly target the active
view (`m_activeViewContainer`, `DolphinViewActionHandler::m_currentView`). A shared `QAction` has
one state and does not know which toolbar triggered it. Decision: **bound action sets** — each
file view gets a `WhaleViewCommandScope` with a private `KActionCollection` whose target container
is fixed at creation and never changes:

- a second `DolphinViewActionHandler` instance bound once to the own `DolphinView` (Dolphin does the
  same in `DolphinPart`), so view modes, zoom, sorting, roles, previews, hidden files and the file
  actions keep their logic and track their own view's signals;
- a `WhaleViewActions` binding to the own view host (plugin views, flat view, capability gates);
  window-wide services moved into `WhaleViewServices` for this (TB1);
- small `WhaleContainerCommands` with an explicit container for the few window actions (back,
  forward with history menus, up, home, reload, stop, filter, later search per view); middle click
  opens the target in a new tab.

These collections never enter the XMLGUI factory, so they appear neither in the shortcut dialog nor
in "Find Action". Their shortcuts are removed (otherwise visible toolbars cause "ambiguous shortcut"
conflicts); shortcuts stay global and act on the active view. Action IDs equal the global ones. The
scope is created lazily on the toolbar's first show. Interacting with a file view toolbar activates
its view (as the location bar did), so the next shortcut goes to the same view.

**Rejected.** One shared action with the target taken from the toolbar context (no per-view state,
Dolphin's slots still use the active view); proxies for everything (would rebuild the handler's
menus, duplicating logic).

**Ownership and naming.** The `DolphinViewContainer` owns its `WhaleFileViewToolBar`; a
`WhaleToolbarManager` per window holds the descriptors (`global`, `file-view`). No code speaks of
"left" or "right" (a lint, `whale_toolbars_no_pane_sides`, enforces it), so split views, tabs and
future docked views follow from the containers' lifetime. UI names: "Main Toolbar" (KDE's term) and
"File View Toolbar" ("pane" loses its meaning with dockable views, "View Toolbar" collides with the
View menu). Toolbars are icon-only with small icons; unsupported actions are disabled, not hidden.
Defaults and existing users: UX-006.

## Location bar per view (TB7 / LB0–LB5, 2026-10-17 to 2026-10-19)

**Decision.** The location bar becomes an element of the file view toolbar
(`whale_location_bar`, at most once, removable like everything in KEditToolBar, default position
after Reload). Dolphin already separates history (the container's internal navigator) from the
visible navigator, a replaceable UI proxy attached with `DolphinViewContainer::connectUrlNavigator()`;
that API is enough. `WhaleLocationBar` (one per container, a `DolphinUrlNavigator` plus the Trash,
network folder and KDE Connect buttons and the `remote:` logic) always exists and is connected,
also when it is not placed or the toolbar is hidden, because Dolphin calls
`activeViewContainer()->urlNavigator()` without null checks (Ctrl+L, F6, Select All, new tab).

- The global double navigator (`DolphinNavigatorsWidgetAction`, `url_navigators`) is no longer
  created: `connectUrlNavigator()` allows exactly one navigator per container. Entries in user files
  are ignored silently by KXmlGui and stay (downgrade-safe).
- `replace_location`/`editable_location` stay global (shortcuts act on the active view).
- Hidden file view toolbars hide the location bar (no second switch). Ctrl+L does nothing then, as
  with a hidden main toolbar in Dolphin.
- New main toolbar default: spacer, split, stash, search, hamburger menu (buttons stay right).
- Migration (once, with a backup): users with an own file view toolbar file get the location bar
  inserted; users who had hidden the file view toolbars get them shown once, otherwise they would
  have no location bar at all.
- The focus indicator appears only with more than one visible view, as in Dolphin.

**Wrapping instead of overflow (L16, 2026-10-18).** In the overflow menu the location bar is not
usable, so only the location bar wraps into a second row over the full width of the toolbar when
less than 200 px would remain for it in the first row; buttons stay in row 1 and only they overflow.
All views of a tab wrap together so the file lists start at the same height. The second row is a
bottom content margin of the toolbar with the bar placed by hand (QToolBarLayout lays out in
`contentsRect()`); the decision waits until the width has been stable for 25 ms, otherwise opening
a split (the new view grows by animation) made both views wrap and jump back.

**Search per view (E8 revised).** `toggle_search` per view in `WhaleContainerCommands`: opens the
search bar of its own view; Ctrl+F and the global button keep acting on the active view.

**Split button in the file view toolbar (2026-10-18).** A fixed `>>`/`<<` button at the right end
of the first view's toolbar (not configurable, never in the overflow, no shortcut): `>>` opens the
split view (Dolphin's `split_view`), `<<` always closes the right view regardless of the "close
split view" setting (`DolphinMainWindow::closeRightSplitView()`, additive). The global split button
keeps Dolphin's behavior. After upstream's "Split View To Tabs" (merged 2026-10-19) the button got
a context menu: move the right view to a tab or pop it out into a window (see VS).

## Expansion in Compact view (CX1, CX2, 2026-10-19)

Groundwork for linked split views: a selection spanning folders can be shown in any view that can
expand branches.

- **CX1:** Compact view can expand folders like Details (`CompactMode/ExpandableFolders`, default
  on, "Folders: Expandable"). Item width and text position include arrow and indentation (one level
  = item height); the highlight does not cover them. In Compact, Left/Right still change the column
  and Up goes to the previous item (Dolphin's jump to the parent stays a Details feature). Expanded
  folders survive switching between Details and Compact. Compact shows only arrows, no tree lines
  (Breeze draws line stubs with `PE_IndicatorBranch`, so `PE_IndicatorArrow*` is used).
- **CX2:** expand or collapse a whole branch: Alt+click on the arrow (only the arrow) and `*`;
  `+`/`-` expand and collapse; in Compact, Details and the Folders panel (same controller); the keys
  go to type-ahead search when no expandable item is current or selected.
  `KItemModelBase::setExpandedRecursively()` (default = `setExpanded()`); `KFileItemModel` expands
  loaded subfolders level by level in `slotCompleted()` (`continueRecursiveExpansion()`), never
  through symbolic links (a link to a parent would never end). Limit: 10,000 added items
  (`setRecursiveExpansionLimit()`), then stop with an info message (silent in the Folders panel).
  Recursive collapse ends running expansions and forgets remembered expanded children; a normal
  collapse keeps them, as in Dolphin.

## Expansion tints, folder names (ET, EL, ED; 2026-10-24/25)

**Problem.** In Compact view an expanded subtree wraps into the next column: at the top of that
column it is unclear which folder the entries belong to and where the subtree ends.

**Tints (ET).** The background of expanded subtrees is tinted by depth with cycling colors: an item
of depth d gets, for each level k = 1…d, an area from the indentation of level k to the column end
in color `colors[(k − 1) % n]`, so nested blocks keep a strip of the outer colors on the left.
Depth 0 stays untinted; a small untinted margin separates neighboring columns. The row of an
expanded folder followed by its children takes the children's color from the end of its arrow.

- 2 to 10 colors; presets Off, Printer Paper, Grey, Pastel (Vivid) (default), Pastel (Subtle), and
  Custom. Presets are hue and strength mixed into `Base` (Grey: `Text` into `Base`), so they work in
  light and dark themes; custom colors are used exactly as chosen. No blue in presets (confusion
  with selection and hover). Pastel order chosen so neighbors differ, also across the cycle.
- Global, not part of styles; UI in the Highlighting tab ("Expanded Folders"), split from the rules.
- Painted by the view, not by the item widgets (they are only as wide as their size hint and
  would give ragged edges): `KItemListExpansionTintItem` at z = −1 behind the widgets, because
  item widgets stack behind the view and anything the view paints itself would cover them.
- Settings in `ExpansionTintSettings`, loaded by `DolphinItemListView` like the highlighting rules.

**Folder names (EL).** Each run of rows of the same depth in a column shows the names of all its
ancestors side by side, centered on the run, rotated bottom to top, each in the strip under its own
arrow (never over subfolder arrows); drawn only if the whole name fits (no eliding). Text color
with adjustable opacity (default 40 %), font 92 % of the item font. Works also with colors off.
`KItemModelBase::expandedParentIndex()` (default: search backwards; `KFileItemModel` answers from
its parent data). Clicking a strip (collapse / jump to the folder) was left for later.

**Details view (ED).** Same tinting, but only in the Name column. Names are drawn for every
expanded folder whose content is visible, "sticky" at the top of the visible part of its block
(view top or just below the folder row), only if fully visible. With alternating row backgrounds,
the background item paints the row stripes itself and shifts the tint by the same small amount
(`AlternateBase − Base`). Two settings sets, Compact (`[ExpansionTint]`) and Details
(`[ExpansionTintDetails]`), with a switch in the dialog whose preview follows the selected view.
Because colors and names collide with Details' tree lines, Details has a preset "Classic (tree
lines)" (no colors, names disabled, tree lines on); all other presets show arrows only
(`setTreeLinesShown()`). Whether a folder is the head of its block is detected from the next item
rather than `isExpanded()`, which is missing right after a view switch.

## Linked split views (LK1–LK7, 2026-10-20 to 2026-10-22, view API 10)

**Use case.** Clicking an item in one view shows the same item in the other, e.g. the treemap on
the left and the file list on the right.

- **Per tab**, only with an open split view; owned by `WhaleSplitLock` (`src/whale/splitview/`), one
  line in `dolphintabpage.cpp`. A chain-link toggle sits fixed at the left end of the second view's
  file view toolbar (`setLeadingWidget()`, not configurable), clearly visible because it changes
  selection behavior.
- **Location is linked too** (E1): both views navigate together; when linking, the second view
  takes the location of the active one. Navigating to a location that cannot be linked (anything
  but a local folder: FTP, SFTP, SMB, `trash:`, search results; `isLocalFile()` and not a search
  URL) unlinks with a short message; returning does not relink. Not stored in the session (off after
  a restart), but part of styles (LK7b).
- **Lockable views** need the capability `LinkedSelection` (API 10): the file list and the treemap
  have it; a view without it disables the toggle (tooltip names the reason) or unlinks.
- **Each view keeps its view mode** (E5): with "same display style for all folders" both views
  would otherwise take the last written common mode on every folder they enter together.
  `DolphinView::setViewModeKept()` (about 20 lines) applies a location's properties without
  overriding the mode; the dynamic view (icons for image folders) does not switch either.
- **Selection rules while linked** (A1–A8): multi-selection only inside one folder. Ctrl+click in
  another folder is refused; Shift+click, rubber band and Shift+arrows are clamped to the anchor's
  folder; Select All and Invert Selection act on the folder of the current item (in an expanded
  tree possibly a subfolder). Each refusal shows a transient message. Without the link the
  selection behaves as in Dolphin.
- **Mirroring** both ways with echo protection: selection and current item; missing items
  (filtered, hidden) are ignored silently.
- **Following into folders (LK7a, replacing LK4).** Expanding branches in the other view to show a
  deep selection opened huge trees that never collapsed, and Icons view could only mark the top
  folder. Now the folder of the active view's current item decides: if the other view can show the
  item in place (the treemap below its location, a branch the user expanded, the flat view), it
  selects it there; otherwise it moves into that folder and selects it there, and moves back when
  the selection returns. It never expands anything by itself, and the active view stays where it
  is. These moves create no history entries of their own as far as `KUrlNavigator` allows (it cannot
  replace or delete entries): the first move adds one entry above the own location, later moves
  replace it, returning is `goBack()`.
- **Transient messages** (`WhaleTransientMessage`): an own `KMessageWidget` floating at the bottom
  center of the view area, transparent for the mouse, gone after 4 s, not stacked; a child of the
  container (as a child of the view host a view switch covered it). Dolphin's message bar stays
  untouched.

## Panel identity (PI0–PI4, 2026-10-21)

**Bug.** Split view with Compact on the left and Icons on the right, quit: after a restart one
view in Icons. With the default "same display style for all folders" (`GlobalViewProps=true`)
every folder and *both* views shared one set in `view_properties/global`; `setViewMode()` writes
immediately, so the view changed last won. The split state itself was never remembered. Upstream
behaves the same; the code predates split views.

**Decision.** Each view has a role by position, `Left` or `Right`, and in global mode each role has
its own complete view properties set (`global`, `global-right`; the right role falls back to the
left set until it has its own, so nothing changes for existing users). The role follows the
position: when the left view closes, the right one moves left and writes its state into the left
set. The role is passed in the constructor (otherwise a new right view would read the left set
first). A right view that appears writes its inherited style at once as its own. The View settings
page writes both roles only when switching from per-folder to global. The KPart and callers without
a role use `Left`. Folder mode (`GlobalViewProps=false`) is untouched.

The "open new windows in split view" checkbox became `SplitViewAtStartup` = Last State (default) /
Always Split / Always Single; the old `true` migrates to Always Split. Last State is written on every
split change and when the window closes. Session restore still has priority.

PI0 fixed an upstream bug in "Split View To Tabs": with "close inactive view" the active view
stayed and was duplicated while the other was lost.

## Own view style (VS0–VS4, 2026-10-21)

**Bug.** "Pop out Right View" and "Split View To Tabs" showed the left view's style: a view had no
style of its own. `DolphinView::setUrl()` re-reads the view properties on every folder change and
every change is written immediately — designed for per-folder styles; the global mode only lets
every folder point to the same file.

**Decision.** In global mode each `DolphinView` owns its style in memory (`ownStyle()`):

- created from its role's set; a folder change reads nothing and applies the own style;
- changes go into the own style and the whole own style is also written to the role's set, only to
  seed new views (start, new tabs, opening a split); per role the last change wins;
- places with their own properties (trash, search, recent files, downloads) still read their set,
  and leaving them returns to the own style;
- the dynamic view (icons for image folders) changes only the display, not the own style;
- Apply in the settings dialog does not re-read (otherwise every view would jump);
- moving a view to a tab or window takes its style along in memory; a pop-out window is a new
  process and gets it through the hidden option `--view-style` (serialized view properties); the new
  window closes a split it would have got from "Always Split";
- the View Properties dialog shows the view's own style.

`setViewModeKept()` (LK) stays for folder mode and the dynamic view. Folder mode is unchanged; the
logic stays in one helper and a few hooks to keep upstream merges manageable.

## Styles (ST1–ST4, 2026-10-22, extended up to 2026-10-23)

**Shown as "Profiles" (2026-11-06).** "Style" collided with Dolphin's "display style" (view mode,
sorting, hidden files and so on of one view), so a style contained "Style of the left view". The
UI now says "Profiles" (precedent: Konqueror's view profiles); "Layouts" stays free for a future
arrangement of several windows (as in Directory Opus). Internal names keep "style": `Whale*Style*`
classes, the `whale_styles` action names, `orcinusstylesrc` and its keys.

**Goal.** One click or shortcut switches the whole look of a window, modeled on Directory Opus'
lister styles. The maintainer's two daily styles: "Plain" (one view, Compact, no menu bar, no
toolbars, no panels, only the status bar) and "Pictures" (one view, medium icons, previews, the
Information panel).

**Model.** A style has a stable ID (derived from the name, kept on rename because action name and
shortcut depend on it), a name, an optional shortcut and elements. An element is one part of the
window with a key, a checkbox ("set this") and a serialized value; an unchecked element leaves that
part alone (Directory Opus' principle). Each element type is a class with `capture`, `apply`,
`describe` and `isAvailable`, registered in `WhaleStyleElements` in a fixed apply order (layout,
then views, then furniture); adding an element is a class and a registry entry.

**Elements.** `split`; `viewStyle.left/right` (the view's own style, global mode only — in folder
mode disabled with a hint, overwriting the current folder's style would contradict that mode);
`view.left/right` (file list or plugin ID, shown without writing a folder preference; missing
plugin → file list); `splitLock`; `zoom` (global per mode, icon and preview size);
`menuBar`; `toolBars` (by object name); `fileViewToolBars`; `statusBar`; `panels`; filter bars
(FB3). Apply order: split, view styles, shown views, link, then the rest. Elements that cannot be
captured are disabled; elements from a newer version are kept and written back unchanged.

**Storage.** Own file `orcinusstylesrc` (configuration, not state), `[General] Version=1`,
`Order=`, groups `[Style][<id>][Element][<key>]`. Forward compatible (unknown entries kept).

**Use.** View → Styles lists all styles, "Save Style "…"" (re-captures the style applied or saved
last, keeping ID, shortcut and checkboxes; remembered per window and as `[Styles] LastStyle` for
new windows) and "Save as Style…" (asks for a name; an existing name is replaced after a question).
Applying acts like a user change and therefore also becomes the start state of new windows, tabs
and splits (E3); other open windows stay as they are; no "active style" indicator. Styles also
appear in the hamburger menu and in the view background's context menu while the file view
toolbars are hidden — together with Ctrl+M and the shortcuts this is the way back from "Plain".

**Shortcuts.** A test against KXmlGui 6.24 showed that it applies saved shortcuts only to actions
that exist at `setupGUI()`; style actions are created later. So `orcinusstylesrc` is the only
source: actions `whale_style_<id>` get the style's shortcut (no default), changes in "Configure
Shortcuts" are written back to the styles after `KShortcutsDialog::saved`, and stale
`whale_style_*` entries are removed from the `ActionProperties` (as upstream's
`ServiceMenuShortcutManager` does).

**Settings page "Styles"** (between View and Context Menu): list with reordering, "New from
Window", rename, delete, shortcut (`KKeySequenceWidget`, conflicts refused), elements with
checkboxes and descriptions, "Capture Again", "Apply to Window" (immediate). The page edits a copy
saved with Apply/OK. Known: if another window saves a style while the dialog is open, OK overwrites
that change.

## Filter bar (FB1–FB3, 2026-10-22)

- **Keep filter by default (FB1).** Dolphin creates the filter bar with "Keep Filter When Changing
  Folders" off and clears the text on every folder change; users took it for a bug. The lock is
  now on by default and remembered (`[FilterBar] keepFilter` in the state config, like case and
  mode); closing the bar clears only the text.
- **Filter scope (FB2).** Dolphin matches every item's name, so `*.jpg` hides all folders. New
  setting Files (default) / Folders / Files and Folders: items outside the scope always match.
  `KFileItemModelFilter::Scope` defaults to `FilesAndFolders`, so the KPart and other users are
  unchanged; the filter bar's default `Files` deliberately differs from Dolphin. A button with a
  menu after the mode button, remembered as `[FilterBar] filterScope`.
- **Filter bars in styles (FB3).** Elements `filterBar.left/right` with visibility, lock, case,
  mode, scope and the text (text last, may contain commas), e.g. media on one side and their
  companion texts on the other.

## Menu bar as a toolbar element (TM1–TM3, 2026-10-22)

The whole menu bar can be placed in a window toolbar as the element "Menu Bar"
(`whale_menu_bar`, not offered for file view toolbars, not in the default toolbar):

- An own non-native `QMenuBar` with the same `QMenu` objects as the real menu bar, kept in sync by
  an event filter. Independent of the classic menu bar (Ctrl+M only affects the latter).
- All or burger: either all menu titles side by side at a fixed width, or the element turns into a
  burger button whose menu holds the menus as submenus. Computed from the toolbar width minus the
  other elements (stretchable ones with their minimum size), with 4 px hysteresis; vertical
  toolbars always use the burger. `QToolBar` would otherwise push the element into its overflow.
- While the element is in a toolbar, the `KHamburgerMenu` button of the same toolbar gets width 0
  (KHamburgerMenu and `QToolBar` control its visibility themselves, `hide()` would not stick).
- `KAcceleratorManager::setNoAccel()` on the embedded bar: the window's accelerator manager gave the
  same title different mnemonics in both bars and wrote them alternately into the shared actions,
  making both bars flicker on hover.

## Built-in viewer (V1–V3, 2026-10-24/25)

**Goal.** Find the best photo in full quality, close the viewer with Esc, and have that photo
selected in Orcinus. Directory Opus can link its viewer to the file list, but the link has to be
made on every opening; Orcinus always couples, without a switch.

**Behavior (V1).**

- Opens through the normal activation (single or double click as configured in KDE, Enter); hooked
  into `DolphinViewContainer::slotItemActivated()`/`slotItemsActivated()` next to the script
  launcher. Only local files whose MIME type is enabled on the "Viewer" settings page (default
  `image/*`, with libmpv also `video/*`) and that Qt can decode; other files and non-local URLs open
  as before. The "open %1 items?" question does not count items the viewer shows.
- Browses only displayable files of the opened file's folder, in the view's order (sorting,
  filter, hidden files), no expanded subfolders. With more than one displayable item selected when
  opening, only through those (V-E3). Whether the selection then stays and only the current item
  moves (A) or the selection moves too (B) is a hidden setting (`[Viewer] MultiSelection`), kept
  after trying both.
- Coupling: every change sets the current item and the selection in the view it was opened from,
  and scrolls there (`DolphinView::showViewerItem()`).
- A borderless full-screen window on the monitor of the Orcinus window, no visible controls, the
  pointer hides after a moment. Mouse wheel with accumulation (fast turning skips images without
  loading them; changes within 40 ms are merged), arrows, Page Up/Down, Backspace, Home/End,
  clicks on the left/right fifth; a click into the image does nothing; Esc closes. The file name
  fades in briefly; "First file"/"Last file" at the ends; wrap-around optional (default off).
  Load errors show the name and stay on that position. Deleting is not offered.

**Quality and speed (V2).** Decoding and scaling in a process-wide pool of two threads (the
current image first; Esc does not wait). Cache: current image, one back and two ahead in browsing
direction, full images of neighbors within 512 MB, otherwise only the fitted one;
`QImageReader` limit raised to 2 GB for panoramas. EXIF rotation; images with an embedded
profile are converted to sRGB (KWin treats windows without a profile as sRGB and maps to the
screen); CMYK and grey to RGB. Never enlarged beyond 100 % when fitting, unless "Enlarge small
images to fill the screen" (always smooth). Zoom in device pixels per image pixel (100 % = one
image pixel per screen pixel, also on HiDPI): Ctrl+wheel towards the pointer, `+`/`-`, `1` (100 %),
`0` (fit), up to 3200 %, middle button pans, browsing fits again; above 100 % sharp pixels by
default ("Smooth the pixels when zoomed in" optional). Only the visible part of the full image is
drawn. Animated GIF/WebP loop with `QMovie` while shown.

**Video (V3).** libmpv, optional at build time (`WHALE_WITH_MPV`, `HAVE_MPV`). `WhaleVideoPlayer`
(mpv with `config=no`, no own keys or OSC, `keep-open`, `hwdec=auto-safe`; offscreen `vo=null`/
`ao=null`; `LC_NUMERIC=C` as libmpv requires) and `WhaleVideoWidget` (`QOpenGLWidget` with the mpv
render API, transparent for the mouse, no focus, so the viewer keeps keys and mouse). Name, hints
and controls are drawn by a transparent overlay widget on top of the video. Defaults: autoplay,
sound and loop on, all configurable. Keys: Left/Right seek 5 s, everything else browses as for
images, Space plays/pauses, M mutes; zoom keys do nothing for videos. A self-drawn control bar at
the bottom while the pointer is visible, while dragging and while paused (play button, progress,
time, "Muted"); clicks on it never browse. A left click on a video pauses or resumes; the edge
fifths still browse. Videos are not cached or preloaded; one player for all.

**Open.** Opening from plugin views and coupling to their selection; decoder crashes (RAW, HEIF)
would hit the Orcinus process and might need isolation later.

## Information density (ID1–ID4, 2026-11-12)

**Goal.** More items per screen, as in Directory Opus (18 px rows against Dolphin's 22 px at 16 px
icons and Noto Sans 10pt). The row height is `2 * padding + max(iconSize, lineSpacing)` with a
hard coded padding of 2 and Noto's line spacing of 18 px for a cap height of 9 px.

**Decisions.**
- Per view mode, in the `*ModeSettings` (`Density`, `CustomPadding`, `CustomLineHeight`):
  Normal 2 px, Compact 1 px, Very compact 0 px, Custom 0–10 px plus a line height of 50–150 % in
  Compact and Details. A tighter line height was rejected as a preset (descenders and accents
  touch the selection or the next row) but is offered under Custom.
- Only the *vertical* padding: `KItemListStyleOption::verticalPadding` (-1 = `padding`) and
  `lineHeightPercent`, used for row heights (informant size hints, `updateGridSize()`) and the
  vertical positions in the widget. `padding` stays 2 for everything horizontal; with 0 the name
  would touch the icon and the Details columns each other. Icons ignores the line height (names
  wrap over several `QTextLayout` lines). A Details row lower than the font centers its text.
- Compact column gap `4 + 2 * verticalPadding` (8/6/4); `4 * padding` would reach 0.
- kcfg default Normal (existing users keep Dolphin's look, "Defaults" means Normal); `main.cpp`
  writes Compact on the first start unless a `Density` key exists (until the first window is
  closed normally every start counts as the first).
- Upstream's Icons option "Spacing: Compact" (gap between items) stays separate.
- Profile element `density` after `zoom`; the shipped profiles do not set it, so switching
  between them keeps the user's density.

**Not done.** Window chrome (toolbar, location bar, filter bar, status bar) is the next
candidate for density.

## Bar density (TD0–TD5, 2026-11-12)

**Goal.** Lower bars, as in Directory Opus (about 2/3 of the height). With Breeze a toolbar is 46 px:
a 34 px tool button (22 px icon + 2 × `PM_ButtonMargin` 6) plus 2 × `PM_ToolBarItemMargin` 6.
Smaller icons alone give only 43 px: line edits (31) and combo boxes (32) then set the height.

**Decisions.**
- One global setting (`[WhaleToolbars] ToolbarDensity`, `WhaleToolbarSettings`): Normal,
  Compact, Very compact; no Custom. Normal is Dolphin's look; new users get Compact (first start).
- The rows stay: merging the main toolbar and the file view toolbar into one row (as Directory
  Opus does) was rejected by the maintainer; whoever does not want the file view toolbar hides it.
- `WhaleDensityStyle`, a `QProxyStyle` around the application style, because
  `QWidget::setStyle()` does not reach child widgets. It changes only widgets inside a
  `QToolBar`, `FilterBar` or `DolphinStatusBar` (found by walking up the parents). With Normal no
  proxy is installed at all. A new application style (Plasma settings) is wrapped again; a change
  of the setting recreates the application style, so Qt re-polishes everything.
- Compact halves the toolbar margin and lowers tool buttons by 4 px; fields keep their height.
  Very compact: margin 1, buttons, fields, combo boxes and push buttons 8 px lower, never below
  their contents. Breeze draws a line edit flat below 2 × 6 px + the font height (31 px); the
  maintainer prefers the flat fields, so the planned font metrics trick (TD2) was dropped.
- The proxy carries `Q_CLASSINFO("X-KDE-CustomElements", "true")` and the base style's object
  name: KStyleExtensions asks only such styles for custom elements, and KCapacityBar (status bar
  space info) otherwise paints its old gradient.
- Dolphin code, marked: `DolphinStatusBar::updateDensityHeights()` (Dolphin takes the stop
  button's height before layout, i.e. Qt's default 30 px; with a density its size hint) and
  `AnimatedHeightWidget::changeEvent()` (a shown bar takes its new height after a style change).
- Setting: Interface → Status & Location bars → "Bar density" (there is no General page), not in
  the KCM. Profile element `toolbarDensity` after `density`; the shipped profiles do not set it.

**Result.** Top bars 94 → 78 (Compact) → 61 px (Very compact); status bar 30 → 24 → 20 px.

## Default file manager and "Show in folder" (plan from 2026-10-03, FileManager1 not implemented)

**Opening folders** (`xdg-open`, KIO, "open folder" in other programs): Orcinus appears in System
Settings → Default Applications → File Manager through `Categories=FileManager` and
`MimeType=inode/directory`; from the command line `xdg-mime default io.github.arkarit.orcinus.desktop
inode/directory` (writes `~/.config/mimeapps.list`; removing the line returns to the system default
from `kde-mimeapps.list`). Meta+E is reassigned in the shortcut settings. Dolphin stays usable;
configuration and instances are separate.

**"Show in folder"** (browsers, mail programs, KIO's `highlightInFileManager`, the KDE portal) goes
through D-Bus `org.freedesktop.FileManager1`. Mechanics checked on a Plasma system:

- Every GUI instance of Dolphin (and of Orcinus with `WHALE_PROVIDE_FILEMANAGER1=ON`) registers the
  name with `QueueService`: whoever runs first holds it, the others queue.
- A service is only started when nobody holds the name. The session bus runs with systemd
  activation; Dolphin's service file names `SystemdService=plasma-dolphin.service`, which starts
  `dolphin --daemon`. `~/.local/share/dbus-1/services` comes before `/usr/share/dbus-1/services`.
- Without FileManager1, KIO falls back to opening the folder through the MIME association, without
  highlighting the file.

**Problem of today's option:** with `ON`, CMake installs `io.github.arkarit.orcinus.FileManager1.service`
into the system D-Bus service directory next to Dolphin's file with the same `Name=`; which one wins
is undefined, and it applies to all users.

**Planned opt-in per user:**

1. With `ON`, install `plasma-orcinus.service` (inactive until referenced) and the D-Bus service
   file only as a template under the data directory, never into the D-Bus search path; the identity
   test guards that. Development builds stay `OFF`.
2. The user links the template into `~/.local/share/dbus-1/services/`, reloads the bus configuration
   (`busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig`)
   and closes all Dolphin windows (or logs in again) so the name becomes free.
3. Check without GUI: call `SortOrderForUrl` on `org.freedesktop.FileManager1` (activates the
   service), then the owner should be `orcinus --daemon`; a started Dolphin must only queue; then
   "Show in folder" from Firefox and a KDE application.
4. Way back: remove the link, reload, stop `plasma-orcinus.service`. Dolphin's system files are never
   touched.

**Open:** if Dolphin starts first in a session it keeps the name until all its windows are closed;
remedy: start `plasma-orcinus.service` at login (one idle process per session). That the user
service directory wins for the same `Name=` and that links are followed is per specification but
was not tested yet. The daemon logic in `main.cpp`/`dbusinterface.cpp` is upstream code behind
`#if WHALE_PROVIDE_FILEMANAGER1`.
