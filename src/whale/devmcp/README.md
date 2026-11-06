<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Whale development MCP

Development-only UI test automation for Whale. It makes the internal UI state
(panes, views, focus, location, sorting, filter, hidden files, selection) machine
readable and lets tests drive a small, fixed set of non-destructive UI commands.

It is **not** a remote control, not an API, not ABI stable and never part of a
regular build. Anything that can be done reliably with a shell, KIO, CMake or
CTest does not belong here.

## Design

Three ways were considered (2026-10-05):

| Option | Verdict |
|---|---|
| Separate MCP server with its own IPC channel | An own channel is unnecessary, D-Bus is already there; in practice the same as the chosen option. |
| MCP server inside the Whale process | Whale would have to serve JSON-RPC over stdio or a socket: MCP code in the product, more attack surface. Rejected. |
| **Diagnostic object on D-Bus plus a thin adapter** (chosen) | Whale knows nothing about MCP; local and bound to the session, no port; state is queried deterministically in-process; screenshots through `QWidget::grab()` work on Wayland without a portal. |

The existing Appium tests (`appiumtests/`, Selenium over AT-SPI) were not reused: they only
see the accessibility tree (no view IDs, focus proxies or hidden Dolphin children), and the
kitemviews accessibility crashed in `filenamehighlightingviewtest` at the time.

Guiding question for every tool: can it already be done reliably with a shell or a test?
Then it does not belong here.

## Layout

| Part | Where | What |
|---|---|---|
| Automation | `automation/whaledevautomation.*` | Inside Whale. Knows Whale/Dolphin internals, reports semantic JSON, runs the commands. |
| D-Bus face | `automation/whaledevautomationdbus.*` | Exports exactly the listed methods on `/orcinus/DevAutomation` (interface `io.github.arkarit.orcinus.DevAutomation`) of the instance's own session bus name `io.github.arkarit.orcinus-<pid>`. |
| MCP adapter | `adapter/whale_dev_mcp.py` | Separate process, stdio MCP server. One tool = one D-Bus call; no logic, no state. Python standard library + `python3-dbus`. |
| Launcher | `whale-dev-session.sh` | Starts the build tree's Whale with the automation in a profile of its own. |
| Tests | `tests/` | In-process test of the automation; end-to-end M3–M7, M9, T1, T2, O3, F1 and TB acceptance through the MCP (`test_m3_acceptance.py` … `test_m7_acceptance.py`, `test_m9_acceptance.py`, `test_t1_acceptance.py`, `test_t2_acceptance.py`, `test_o3_acceptance.py`, `test_f1_acceptance.py`, `test_tb_acceptance.py` (multi-toolbar, TB6), `test_lk_acceptance.py` (linked split view), `test_v1_acceptance.py` (the built-in viewer), `test_v2_acceptance.py` (its zoom, preloading and animations), `test_v3_acceptance.py` (videos, with libmpv), `test_v3_render_virtual.py` (videos really drawn, by hand: an invisible `kwin_wayland --virtual` on a D-Bus session of its own, needs Pillow), `test_f1_wayland_real.py` (real drags on Plasma/Wayland), `test_lk_wayland_real.py` (the linked split view with real clicks on Plasma/Wayland), shared `mcp_test_support.py`; `wayland_real_input.py` for real input on Plasma/Wayland). |

## Activation

1. Build time: `cmake -DWHALE_ENABLE_DEV_MCP=ON` (default OFF). Without it nothing of
   this directory is compiled; `whale_dev_mcp_default_off` checks that.
2. Run time: the environment variable `WHALE_DEV_MCP=1`. Without it Whale creates
   nothing and registers nothing.

Only the local session bus is used: no network, no sockets, no generic object
access, no method other than the ones below.

## Use

```bash
src/whale/devmcp/whale-dev-session.sh [--offscreen] /some/folder   # prints the PID
src/whale/devmcp/adapter/whale_dev_mcp.py call get_ui_state          # one call from a shell
claude mcp add whale-dev -- python3 $PWD/src/whale/devmcp/adapter/whale_dev_mcp.py
```

With several automated Whales running, `WHALE_DEV_MCP_PID=<pid>` picks one.
The test views Hello (`whale_hello`) and Test List (`whale_testlist`) are test fixtures,
not part of the product: they live in the build tree's test plugin directory
(`<build>/src/whale/tests/testviewplugins`). `whale-dev-session.sh --test-views` adds it;
the acceptance tests get it through `--plugin-dir` (a list of directories, separated by `:`).
The launcher's profile (`<build>/whale-dev-profile`) keeps test runs away from the
settings of the Whale used every day; Whale shares view properties between folders
by default, so sorting or hidden files would otherwise leak into it.

## Tools

| Tool | Purpose |
|---|---|
| `get_ui_state` | Full semantic state (see below) |
| `get_focus_state` | Only the focus part |
| `set_view` | `dolphin` or a view plugin ID in a pane; an automatic switch, or with `remember: true` the user's choice through the view's View menu entry (for `dolphin` the entry of the file list's current mode, which chooses the file list only while another view is shown; O3: becomes the location's preferred view) |
| `get_preferred_view` / `set_preferred_view` / `clear_preferred_view` | O3: the view remembered for a location (`view`, normalized `key`, `count`); setting it is like a choice in another window (shown views stay) |
| `set_view_plugin_available` | Test hook: a view plugin is treated as missing (`available: false`) or back, in the window's registry |
| `activate_pane` | Make a pane the active view area |
| `set_split_view` | Open/close split view (setter); closing closes the active side, like the Close button |
| `set_split_lock` | LK6: link/unlink the views of the split view (setter) through the lock button; fails while it is hidden or disabled |
| `apply_style` / `save_style` | ST3: apply a style by `id` through its action (as its menu entry or shortcut); save the window's state as a style by `name` (as "Save as Style…", an existing name is captured again), without `name` the style applied or saved last ("Save Style"); `styles` in the state |
| `set_split_ratio` | Move the handle between the halves (`left_share` 0.1–0.9), as dragging it does |
| `set_window_size` | Resize the main window (`window.width`/`height` in the state) |
| `set_location` | Navigate a pane through the location bar |
| `set_filter_text` / `clear_filter` | Filter bar of a pane |
| `set_hidden_files` | Setter, not a toggle |
| `set_content_scope` | F1: `current-folder` or `recursive-files` for a pane's file list through "Show flat (no folders)" (the user's choice, remembered for the location); local folders only |
| `set_sort` | `field` (name, size, modified, created, type, extension), `order`, `folders_first` |
| `set_selection` | Selection (`urls`, optional `current`) of a pane through the host API that plugin views use; URLs outside the listing are returned as `ignored` |
| `activate_items` | Open items of a pane like Enter/double click, through the host API that plugin views use |
| `trigger_action` | The same QActions as the menus, toolbar and shortcuts (since T2 also on a plugin view's own selection, see `command`). Only `focus_other_view`, `go_up`, `go_back`, `go_forward`, `edit_location`, `select_all`, `invert_selection`, `rename`, `properties`, `new_tab`, `close_tab`, `whale_show_file_view_toolbars` (TB5's "Show File View Toolbars"), `activate_next_tab`, `activate_prev_tab`, `show_preview` (toggles "Show Previews"), `reload` (F5), `show_information_panel` (toggles F11), `view_icons`/`view_compact`/`view_details` (the file list's view modes, F1), `zoom_in`/`zoom_out`, and the file operations `copy`, `cut`, `paste`, `trash`, `delete`, `duplicate`, `copy_location`, `copy_to_other_pane`, `move_to_other_pane` (see below). Disabled (gated) actions are refused. |
| `get_action_state` | Enabled state and gating reason of Whale's file/view actions (also `actions` in the state) |
| `open_context_menu` / `close_popup` | Context menu of a pane for an item or the location, through the host API of plugin views; shown as `popupMenu` |
| `request_item_info` | Report an item as hovered (information panel), through the host API of plugin views |
| `scroll_to_item` | Scroll a plugin item view so an item is at its top (a view state; `pluginTopItem`) |
| `set_expanded` | Expand or collapse folders of a pane's file list (Compact/Details) through the model, one level or a whole branch (`recursive`, like Alt+click or `*`); folders not loaded yet are returned as `missing` |
| `quit` | Close the window as the user does: Whale saves its session and quits (restart tests) |
| `rename_dialog` / `close_dialogs` | Answer the rename dialog (file operation) / cancel open dialogs; open dialogs are `dialogs` in the state |
| `get_preview_state` | Preview state of all panes (`previews`, see below) and the answers to `request_preview` |
| `request_preview` | Request previews of items through the preview provider of a pane's plugin view, as the view does |
| `perform_drag_drop` | Drag the selection of a pane and drop it onto a folder item or the location of a pane (`copy`/`move`/`link`/`ask`); file operation, see below |
| `choose_popup_entry` | Choose an entry of the open popup menu by text, e.g. "Copy Here" of the drop menu; file operation |
| `treemap_hover` | Treemap (M9): move the pointer onto the cell of `url`, or off the treemap without `url`; the result has the cell's position in the window (`windowX`, `windowY`) |
| `treemap_select` | Treemap: click into the cell of `url`, then Up until `url` itself is selected (deep entries too) |
| `treemap_activate` | Treemap: `how=enter` (select, then Enter) or `how=double_click` on the cell of `url` |
| `treemap_zoom` | Treemap: mouse wheel `in` at the cell of `url` (one level towards it) or `out` |
| `treemap_key` | Treemap: a key (`Up`, `Return`, `+`, `Escape`, `Delete`, `Ctrl+C`, `Menu`, …); the window's shortcuts get it first; `result.shortcut`. `Menu` is delivered as Qt delivers a real one: a context menu event, then the key press (to an open menu) |
| `treemap_context_menu` | Treemap (T1): right click into the cell of `url` (real input through the window): the deepest entry there is selected and the treemap's own context menu opens (`popupMenu`, `result.menuShown`) |
| `file_operation` | T1: `operation` (`open`, `open_with`, `show_in_folder`, `properties`, `rename`, `trash`, `delete`, `copy`, `cut`, `copy_location`, or `supported`) on explicit `urls` through the `WhaleFileOperations` of the pane's plugin view, the host API its context menu calls; the items need not be listed. Changing ones are file operations (see below) |
| `answer_dialog` | Press a button of an open dialog by its text, e.g. `Delete Permanently` of KIO's confirmation; file operation |
| `treemap_set_options` | Treemap: `color_mode` (`fileType`/`topFolder`) and `labels` through the treemap's own controls |
| `trigger_toolbar_action` | TB6: clicks a toolbar button with mouse events through the window, as the user does: `toolbar` `global` (main toolbar, acts on the active view), a pane index, `active` or a file view's `instanceId` (any tab); `id` is Whale's action name. With `menu_entry` (ID or text, `Submenu > Entry`) the button's menu opens as the user opens it (also a delayed history menu) and the entry is chosen with the keyboard. Errors for unknown toolbars, instances and actions, actions not in the toolbar or disabled (with the reason), missing entries, hidden or never shown toolbars; actions that change files are file operations |
| `location_bar_input` | LB4: enters `text` into a file view's location bar as the user does: a click on its toggle button if it shows the breadcrumb, the text typed over the editor's text, with `submit` Return. `toolbar` is a pane index, `active` or an `instanceId`. Errors for unknown panes and instances and for a location bar that is not placed or not shown. Ctrl+L is `trigger_action edit_location` |
| `configure_toolbars` | TB6: operates KDE's Configure Toolbars through its widgets, in this order: `open`, `select` (`global`/`file-view`), `remove`, `insert` (IDs, `-` separator, `|` spacer; at the end), `defaults` (KDE's question answered with Reset), `apply`, `ok`, `cancel`; returns the dialog state |
| `wait_for_state` | Waits inside Whale until conditions hold (or a timeout); use instead of sleeps |
| `viewer_input` | V1d: input for the built-in viewer, events sent to its window: `action` `key` (`key` as in QKeySequence: Right, Left, PgDown, Home, End, Escape …), `wheel` (`delta`, 120 a notch, negative = next) or `click` (`where` `left`/`right` edge or `center`: nothing on images, pauses a video); V2e: keys `+`, `-`, `1`, `0` zoom, `zoom_wheel` (Ctrl+wheel, `delta`, `at` [x, y] fractions of the window) and `drag` (`dx`, `dy` with the middle button); V3d, videos: keys Left/Right seek, Space, M, `pointer` (the controls show) and `controls` (`target` `play` or a fraction of the progress). The viewer opens with `activate_items` on images |
| `set_viewer_options` | V1d: the viewer's settings as its settings page writes them: `wrap_around`, `mime_types`, the hidden `multi_selection` (`keep`/`move`), (V2e) `upscale`, `smooth_zoom`, (V3d) `video_autoplay`, `video_sound`, `video_loop` |
| `screenshot` | PNG of the main window, or with `target: viewer` of the open viewer, rendered by Whale itself (works on Wayland) |

Panes: `0`/`left` primary, `1`/`right` secondary (split view), `active` (default).

## State format (schema 1)

```json
{
  "schema": 1, "pid": 1234, "url": "file:///…", "splitView": true, "splitAnimating": false,
  "paneCount": 2, "activePane": 0,
  "availableViews": ["dolphin", "whale_hello", "whale_testlist"],
  "panes": [{
    "index": 0, "side": "left", "panelRole": "left", "active": true, "url": "file:///…",
    "viewId": "whale_testlist", "dolphinViewVisible": false, "pluginWidgetVisible": true,
    "viewMode": "icons", "loading": false, "itemCount": 4,
    "items": ["sub", "a.txt"], "pluginRows": ["sub", "a.txt"],
    "selectionCount": 1, "selection": ["file:///…/a.txt"], "selectedNames": ["a.txt"],
    "current": "file:///…/c.txt", "currentName": "c.txt",
    "pluginSelection": ["a.txt"], "pluginCurrent": "c.txt", "statusText": "1 file selected (300 B)",
    "hiddenFiles": false, "sort": {"field": "name", "order": "ascending", "foldersFirst": true},
    "filter": {"text": "", "barVisible": false, "scope": "files"},
    "focusProxy": "plugin-widget", "hiddenDolphinViewHasFocus": false
  }],
  "focus": {"role": "plugin:whale_testlist", "pane": 0, "activePane": 0, "widgetClass": "QListView",
            "windowActive": true, "keyboardFocus": true, "anomalies": []},
  "activations": {"count": 3, "recent": [{"seq": 3, "tab": 0, "pane": 0, "focusRole": "…"}]},
  "shell": {"selectedNames": ["a.txt"], "selectionSignals": 4},
  "tabs": {"count": 1, "current": 0}, "window": {"visible": true, "active": true}
}
```

- `splitLock` (LK6): the lock of the current tab: `locked`, the button (`buttonVisible`,
  `buttonEnabled`, `buttonChecked`, `buttonIcon`, `toolTip` with the reason when it cannot
  lock, `buttonX`/`buttonY` its center in main window coordinates for real input) and per pane `lockable`, `reason`, `selectionLimited` (the selection rule A1 is on).
  The messages of the lock float above the view (`panes.N.transientMessage`, while shown also
  in `message`).
- `panes.N.filter.scope` (FB2): what the filter applies to: `files` (folders stay shown, the
  default), `folders` (files stay shown) or `files-and-folders` (Dolphin's behavior).
- `styles` (ST3): `list` of the styles in their order (`id`, `name`, `shortcut` as stored,
  `action` and `actionShortcut` of the window's action, `elements` with `enabled`, `known` and
  the `description` of each value) and `menu`, the entries of View > Styles (object names, `-`
  for a separator), `current`: the value of each element in the window now (as captured, `null`
  where not available), `menuBarVisible`, the menu bar itself (it follows its action a
  moment later), and `lastStyle`, the style applied or saved last, which "Save Style" saves.
- `viewer` (V1d): the built-in viewer: `settings` (`mimeTypes`, `wrapAround`, `multiSelection`)
  and `open`; while open also the file (`url`, `name`, `index`), its list (`count`, `files`,
  `fromSelection`: opened from a multiple selection), `movesSelection` (the shown file becomes the
  selection, else only the current item), `loaded`, `loadError`, `overlay` (the text shown at the
  bottom, or empty), `visible`, `fullScreen`, `active` and `onWindowScreen`. V2e: `settings` also
  `upscale` and `smoothZoom`; `imageSize` (full resolution), `zoomed`, `zoom` (device pixels per
  image pixel, zoomed or fitted, 3 decimals), `visibleRect` (the visible part in image pixels:
  x, y, width, height), `cached` (names of the files decoded ahead, sorted), `animated`,
  `animating` and `animationFrame` (-1 without an animation). V3d: `settings` also
  `videoSupported` (built with libmpv), `videoAutoplay`, `videoSound`, `videoLoop`; `video`
  (the current file is one), `controlsVisible`, and once a video played `player` (`file`,
  `loaded`, `error`, `position` and `duration` in seconds to 0.1, `paused`, `muted`, `atEnd`,
  `size`, `drawn`: whether it goes to the screen, not on the offscreen platform).
- `panes.N.panelRole` (PI): the position whose global view properties the view reads and
  writes (`left`/`right`). It follows `side`; a view that moves (the left view closes) takes its
  style along.
- `panes.N.ownViewMode` (VS): with one style for all folders, the view mode of the view's own
  style, which it keeps in every folder without a style of its own; it differs from `viewMode`
  where such a place (trash, search) or the dynamic view shows another one. `null` with a style
  for each folder.
- `splitViewAtStartup` (PI3): `choice` (`last-state`, `always-split`, `always-single`) and
  `openInSplitView`, whether a new window or tab opens with a split view now.
- `splitAnimating`: opening or closing the split view is still animating the width of a
  view (a new right view starts 1 px wide). Wait for `false` before anything that depends
  on the geometry of the views, e.g. `perform_drag_drop` onto a view's background.
- `items`: the DolphinView's model order; `pluginRows`: what a plugin item view
  shows (null for other views). Lists stop at 200 entries, counts do not.
- Focus roles: `dolphin-item-view`, `dolphin-rename-editor`, `plugin:<id>`,
  `hidden-dolphin-view` (always a bug), `location-bar`, `filter-bar`, `search-field`,
  `pane-other`, `places-panel`, `terminal-panel`, `folders-panel`,
  `information-panel`, `other-panel`, `toolbar`, `menu`, `other-window`, `other`, `none`.
  The role is that of the window's focus widget; `keyboardFocus` says whether it
  really has the keyboard focus (false while the window is not active).
- Anomalies: `focus-in-hidden-dolphin-view`, `focus-in-inactive-pane`,
  `pane-<n>-focus-proxy-not-plugin`.
- Selection: `selection`/`selectedNames`/`current` are the host's (the DolphinView's)
  selection; `pluginSelection`/`pluginCurrent` what a plugin item view shows;
  `shell.selectedNames` what the main window last sent to the information panel;
  `statusText` the status bar (it follows the DolphinView's debounced selection signal).
- Persistence (M6): `pluginTopItem` (the item at the top of a plugin item view),
  `storedViewStates` (view IDs with a state at the location, including the shown
  view's current one), `restore` (`source` history/session, `requested`, `shown`,
  `fallback`) of the last restore in the pane.
- Previews (M7): `pluginPreviewRows` (rows of a plugin item view that show a preview,
  i.e. a QPixmap instead of the icon), `pluginVisibleRows` (`first`, `last`, `count`),
  and `previews`: `shown` (Dolphin's "Show Previews" at the location), `capability`
  (the view's Previews), `provider` (the view asked for its provider) and, if so,
  `enabled`, `pending`/`pendingCount`, `runningJobs` and the counters `requested`,
  `generated`, `failed`, `canceled`, `jobsStarted`, `jobsKilled`, `maxPending`.
- `activations` counts pane activations, so a test can prove that an operation did
  not activate the other half of a split view.
- Treemap (M9): `treemap` in a pane that shows `orcinus_treemap` (null otherwise), as the
  view describes itself (`treemapState()` of its widget): `status`
  (`scanning`/`ready`/`empty`/`failed`/`unsupported`), `loadingShown`, `error`,
  `statusText`, `scanRoot`, `zoomRoot`, `zoomPath`, `totalSize`, `zoomSize`, `cells`,
  `leafCells`, `restCells`, `truncated`, `hovered`, `selected`, `selectedRelative`,
  `selectedDeep` (below the listing), `selectedShown` (has a cell of its own), `infoUrl`
  (what it reported to the information panel), `colorMode`, `showLabels`,
  `finishedScans`, `lastScanMs`, `scanProgress`, `layoutCount`, `layoutUs`, `renderUs`,
  `paintUs`, `canvasWidth`/`canvasHeight`, `pathText`/`infoText` (its bar).
  `shell.itemInfoUrl`/`itemInfoIsDir` describe what the information panel got last;
  `treeCache` the window's tree scan cache (`hits`, `misses`, `joins`,
  `invalidations`, `evictions`, `notKept`, `entries`, `bytes`, `watchedFolders`,
  `runningScans`).
- View menu: `viewMenu` describes the view modes and view options as the View menus
  offer them for the active pane: `placement` (`direct`: the modes are listed in the
  menus; `submenu`: more than five, all in "View Modes"), `modes` (in menu order: `id`
  `icons`/`compact`/`details` or a plugin ID, `text`, `checked`, `enabled`),
  `viewSettings` (the entries of the toolbar's view settings dropdown, `-` for a
  separator, `name >` for a submenu) and `options.flat`/`previews`/`hidden`
  (`text`, `checked`, `enabled`, `gateReason`).
- Commands (T2): `command` (window: the active pane's; also per pane) is what Whale's
  actions on selected items act on: `source` (`file-list`, `plugin-mirrored` for a plugin
  view that shows Whale's selection, `plugin` for a view with a selection of its own,
  e.g. the treemap) and its `urls`. `actions.<name>.gate` says why an action is off,
  e.g. `nothing selected`, `not supported for the selection`, `not routed to a view's own
  selection`.
- Preferred views (O3): `preferredView` of a pane is the view remembered for its location
  (`dolphin`, a plugin ID, or null); `viewOrigin` says what decided about the shown view:
  `user` (the View menu), `preference`, `history`, `session`, `fallback` (the
  requested view could not be shown), `kept` (a new location without a preference: the
  view stayed), `initial`. `restore.source` is `preference` after a preference was
  applied.
- Content scope (F1): `contentScope` of a pane: `scope` (`current-folder` or
  `recursive-files`, the file list's scope at the location), `shown` (the file list shows
  the files below its location now), `supported` (file list at a local folder), `origin`
  (`user`, `preference`, `default`, `history`, `session`, `initial`), `preferred` (the
  location's remembered scope), `busy`, `fileCount`, `relativeItems` (the shown items
  relative to the location, in the file list's order), `timing` (`scanMs`,
  `projectionMs`, `modelMs`, `updates`), `pasteRefusal` (why Paste is refused there, or
  null), `lastRefusal`/`refusals` (the explanations shown for refused drops and Paste).
  `loading` includes the files below the location the file list still waits for.
- Real input (F1): `fileListPoints` of a pane showing the file list: `items` (`path`
  relative to the location, `x`/`y` the center in main window coordinates, for the fully
  shown items) and a `background` point, e.g. for real drags with ydotool on Wayland.
- File operations (T1): `fileOperations` of a pane with a plugin view: `finished` (count)
  and `last` (`operation`, `result` done/canceled/failed, `urls`, `newUrls`) of what its
  `WhaleFileOperations` reported (rename, trash, delete); `message` the error or message
  shown in the view area. `dialogs` list `type` (rename/properties/other), `class`,
  `title`, `text` (the longest label, e.g. KIO's question), `buttons` and, for properties,
  `urls`. `popupMenu.active` is the entry the keyboard is on. The treemap adds
  `selectedMenu` (the entries of the context menu its selection would get, without showing
  it), `openMenus` (visible context menus), `operationScans` and `lastOperation`.

- Toolbars (TB6): `toolbars.global` is the main toolbar (`visible`, `layout`, `actions`,
  and `menuBar`, the menu bar element of TM: `present`, `mode` `menus` or `burger`, `shown`,
  `width`, `fullWidth`, `menus` (titles), `hamburgerWidth` (room the hamburger menu's button
  takes, 0 while the element is in the toolbar)),
  `toolbars.fileViewsVisible` the setting "Show File View Toolbars", `toolbars.instances`
  the file views of all tabs (`instanceId`, `tab`, `pane`, `visible`, `initialized`).
  `panes.N.toolbar` is the pane's File View Toolbar: `instanceId` (`view-N`, unique in the
  process, the same for the view's life; given by the automation, never from the pane's
  place), `ownerView` (the shown view's ID, as `viewId`), `visible` (not hidden by the
  setting), `shown` (on screen), `initialized` (its actions exist: they are created when it
  is shown first; the state never creates them), `layout` (`-` separator, `|` spacer) and
  `actions` (`id`, `text`, `enabled`, `checkable`, `checked`, `shortcuts`, `actionKey`
  (equal for the same object), `disabledReason`, `gateReason`, `popup`
  (`delayed`/`button`/`instant`), `menu` as it is now: a history menu is filled when it
  opens). `toolbarDialog` is `{"open": false}` or Configure Toolbars' `selected` toolbar,
  `toolBars`, `available` and `current` (`id`, `text` as KDE's lists have them),
  `modified` (Apply enabled) and `buttons`.
- Location bars (TB7, LB4): every file view has its own, in its File View Toolbar.
  `panes.N.toolbar.locationBar`: `exists`, `connected` (it is the view's visible
  navigator), `present` (placed in the toolbar's layout), `wrapped` (in the toolbar's second
  row, L16), `visible`, `editable`, `url`,
  `text` (the editor's, while editable), `placeholder`, `focused`, `active` (the view is the
  active one) and `widgetKey` (`location-bar-N`, the same for the widget's life). The
  entries of `toolbars.instances` have `locationBar` with `exists`, `connected`, `present`,
  `wrapped` and `widgetKey`, also for tabs never shown. The state creates nothing (no actions of a
  toolbar never shown).

`wait_for_state` conditions map dotted paths into this state to expected JSON
values, e.g. `{"panes.1.viewId": "whale_testlist", "focus.role": "plugin:whale_testlist"}`.
Instead of a value, `{"$contains": x}` matches a string containing `x` or an array
containing `x`, e.g. `{"panes.0.statusText": {"$contains": "2 files selected"}}`.

`activate_items` opens files with their real default application. The acceptance
tests (`tests/mcp_test_support.py`) therefore start Whale in a profile in which text
files open with a test "opener" that only logs the path.

## File operations

`copy`, `cut`, `paste`, `trash`, `delete`, `duplicate`, `copy_location`, `copy_to_other_pane`,
`move_to_other_pane`, `rename_dialog`, `perform_drag_drop`,
`choose_popup_entry`, `answer_dialog` and `file_operation` with `rename`, `trash`, `delete`,
`copy`, `cut` or `copy_location` change files or the clipboard. Whale refuses them unless it was started with
`WHALE_DEV_MCP_FILE_OPERATIONS=1` (`whale-dev-session.sh --file-operations`,
`mcp_test_support.start_whale(file_operations=True)`), which is meant for throwaway
test folders only. Such a Whale gets a home inside its profile, so the trash is the
profile's (KIO only uses the home trash for files on the device of `$HOME`), and no
delete confirmation. The state shows the clipboard (`clipboard`) only then, as it is
the user's own otherwise.

## Drag and drop

`perform_drag_drop` changes files and needs `WHALE_DEV_MCP_FILE_OPERATIONS=1`. It does
not copy or move anything itself:

- Source plugin item view: the view gets the user's drag gesture (press on the first
  selected row, moves beyond the drag distance) and starts its drag through
  `WhaleFileOperations::startDrag()`, which builds the drag data and the `QDrag`.
  Only `QDrag::exec()` (the platform's drag transport, which needs a real pointer) is
  replaced for that drag by delivering the events to the target.
- Source file list: the drag data comes from the function the file list's own drags
  use (`KFileItemModel::createMimeData()` + portal export, via the bridge).
- Target: the target view's widget (the plugin item view's viewport, or the file
  list's `QGraphicsView`) gets drag enter, move and drop events and handles them as
  a real drop: target rules, `DolphinView::dropUrls()`, `KIO::drop`.

`action` sets the modifier keys of the drop: `copy` Ctrl, `move` Shift, `link`
Ctrl+Shift, `ask` none (KDE's drop menu opens; `choose_popup_entry`). The reply has
`accepted`/`dropped`, the dragged names and the source path. The test checks the file
system itself.

## Limits

- The state is semantic and immediate; pixels may lag behind: a screenshot taken
  right after a state change can still show an animation (filter bar, split view).
- Navigation, view switching and pane activation go through the same functions as
  the UI, but not through simulated mouse or keyboard input (except the drag gesture
  of `perform_drag_drop` and the treemap tools, which send mouse input through the
  window like QTest and keys through the window's shortcuts first).
- Popup menus (context menu, drop menu) need real input on Wayland (a synthesized click
  has no input serial). Offscreen they work with synthesized input. On a Plasma/Wayland
  session `test_t1_acceptance.py --platform "" --popups real` uses real input instead:
  `wayland_real_input.py` moves the user's pointer with `ydotool` (a running `ydotoold`)
  until a KWin script reports it at the target, and only clicks after the treemap reports
  the target cell as hovered; menu entries are chosen with real Down/Enter keys. It raises
  the test window (found by its PID) above all others while it runs. `--popups off` checks
  the menus through `treemap.selectedMenu` and runs the operations through `file_operation`.
- `test_t2_acceptance.py --platform "" --real-keys` presses F2 and Delete as real keys of
  the session (`ydotool`), only while the test window is active and the treemap has the
  focus; all other shortcuts go through the window's shortcuts (`treemap_key`).
- The clipboard is the user's own outside of offscreen (also in a test profile, as it
  belongs to the compositor), so clipboard steps run offscreen only.
- Two Whales starting at the same moment block each other for up to 25 s (D-Bus timeout):
  at start-up `Dolphin::dolphinGuiInstances()` asks every running instance synchronously
  (inherited from Dolphin). Tests therefore start a second instance only later.
- On Wayland the instance started last gets the activation; a second instance in a test
  therefore always runs offscreen.
- Only the current tab's panes are addressed.
- The automation answers before Dolphin restores its session at start-up; wait for
  the restored state (e.g. `tabs.count`) before acting.
- Restart tests need a private session bus (`test_m6_acceptance.py` starts one with
  `dbus-run-session`): Dolphin restores its session only as the only instance.
- Without a window manager (offscreen) the main window is not activated again after
  a dialog closes; only the logical focus (`focus.role`) can be checked then.
