<!--
SPDX-FileCopyrightText: 2026 Whale contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Whale development MCP

Development-only UI test automation for Whale. It makes the internal UI state
(panes, views, focus, location, sorting, filter, hidden files, selection) machine
readable and lets tests drive a small, fixed set of non-destructive UI commands.

It is **not** a remote control, not an API, not ABI stable and never part of a
regular build. Anything that can be done reliably with a shell, KIO, CMake or
CTest does not belong here.

## Layout

| Part | Where | What |
|---|---|---|
| Automation | `automation/whaledevautomation.*` | Inside Whale. Knows Whale/Dolphin internals, reports semantic JSON, runs the commands. |
| D-Bus face | `automation/whaledevautomationdbus.*` | Exports exactly the listed methods on `/whale/DevAutomation` (interface `io.github.arkarit.whale.DevAutomation`) of the instance's own session bus name `io.github.arkarit.whale-<pid>`. |
| MCP adapter | `adapter/whale_dev_mcp.py` | Separate process, stdio MCP server. One tool = one D-Bus call; no logic, no state. Python standard library + `python3-dbus`. |
| Launcher | `whale-dev-session.sh` | Starts the build tree's Whale with the automation in a profile of its own. |
| Tests | `tests/` | In-process test of the automation; end-to-end M3–M7 acceptance through the MCP (`test_m3_acceptance.py` … `test_m7_acceptance.py`, shared `mcp_test_support.py`). |

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
The launcher's profile (`<build>/whale-dev-profile`) keeps test runs away from the
settings of the Whale used every day; Whale shares view properties between folders
by default, so sorting or hidden files would otherwise leak into it.

## Tools

| Tool | Purpose |
|---|---|
| `get_ui_state` | Full semantic state (see below) |
| `get_focus_state` | Only the focus part |
| `set_view` | `dolphin` or a view plugin ID in a pane |
| `activate_pane` | Make a pane the active view area |
| `set_split_view` | Open/close split view (setter); closing closes the active side, like the Close button |
| `set_location` | Navigate a pane through the location bar |
| `set_filter_text` / `clear_filter` | Filter bar of a pane |
| `set_hidden_files` | Setter, not a toggle |
| `set_sort` | `field` (name, size, modified, created, type, extension), `order`, `folders_first` |
| `set_selection` | Selection (`urls`, optional `current`) of a pane through the host API that plugin views use; URLs outside the listing are returned as `ignored` |
| `activate_items` | Open items of a pane like Enter/double click, through the host API that plugin views use |
| `trigger_action` | Only `focus_other_view`, `go_up`, `go_back`, `go_forward`, `edit_location`, `select_all`, `invert_selection`, `rename`, `properties`, `new_tab`, `activate_next_tab`, `activate_prev_tab`, `show_preview` (toggles "Show Previews"), and the file operations `copy`, `cut`, `paste`, `trash`, `delete` (see below). Disabled (gated) actions are refused. |
| `get_action_state` | Enabled state and gating reason of Whale's file/view actions (also `actions` in the state) |
| `open_context_menu` / `close_popup` | Context menu of a pane for an item or the location, through the host API of plugin views; shown as `popupMenu` |
| `request_item_info` | Report an item as hovered (information panel), through the host API of plugin views |
| `scroll_to_item` | Scroll a plugin item view so an item is at its top (a view state; `pluginTopItem`) |
| `quit` | Close the window as the user does: Whale saves its session and quits (restart tests) |
| `rename_dialog` / `close_dialogs` | Answer the rename dialog (file operation) / cancel open dialogs; open dialogs are `dialogs` in the state |
| `get_preview_state` | Preview state of all panes (`previews`, see below) and the answers to `request_preview` |
| `request_preview` | Request previews of items through the preview provider of a pane's plugin view, as the view does |
| `perform_drag_drop` | Drag the selection of a pane and drop it onto a folder item or the location of a pane (`copy`/`move`/`link`/`ask`); file operation, see below |
| `choose_popup_entry` | Choose an entry of the open popup menu by text, e.g. "Copy Here" of the drop menu; file operation |
| `wait_for_state` | Waits inside Whale until conditions hold (or a timeout); use instead of sleeps |
| `screenshot` | PNG of the main window, rendered by Whale itself (works on Wayland) |

Panes: `0`/`left` primary, `1`/`right` secondary (split view), `active` (default).

## State format (schema 1)

```json
{
  "schema": 1, "pid": 1234, "url": "file:///…", "splitView": true, "paneCount": 2, "activePane": 0,
  "availableViews": ["dolphin", "whale_hello", "whale_testlist"],
  "panes": [{
    "index": 0, "side": "left", "active": true, "url": "file:///…",
    "viewId": "whale_testlist", "dolphinViewVisible": false, "pluginWidgetVisible": true,
    "viewMode": "icons", "loading": false, "itemCount": 4,
    "items": ["sub", "a.txt"], "pluginRows": ["sub", "a.txt"],
    "selectionCount": 1, "selection": ["file:///…/a.txt"], "selectedNames": ["a.txt"],
    "current": "file:///…/c.txt", "currentName": "c.txt",
    "pluginSelection": ["a.txt"], "pluginCurrent": "c.txt", "statusText": "1 file selected (300 B)",
    "hiddenFiles": false, "sort": {"field": "name", "order": "ascending", "foldersFirst": true},
    "filter": {"text": "", "barVisible": false},
    "focusProxy": "plugin-widget", "hiddenDolphinViewHasFocus": false
  }],
  "focus": {"role": "plugin:whale_testlist", "pane": 0, "activePane": 0, "widgetClass": "QListView",
            "windowActive": true, "keyboardFocus": true, "anomalies": []},
  "activations": {"count": 3, "recent": [{"seq": 3, "tab": 0, "pane": 0, "focusRole": "…"}]},
  "shell": {"selectedNames": ["a.txt"], "selectionSignals": 4},
  "tabs": {"count": 1, "current": 0}, "window": {"visible": true, "active": true}
}
```

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

`wait_for_state` conditions map dotted paths into this state to expected JSON
values, e.g. `{"panes.1.viewId": "whale_testlist", "focus.role": "plugin:whale_testlist"}`.
Instead of a value, `{"$contains": x}` matches a string containing `x` or an array
containing `x`, e.g. `{"panes.0.statusText": {"$contains": "2 files selected"}}`.

`activate_items` opens files with their real default application. The acceptance
tests (`tests/mcp_test_support.py`) therefore start Whale in a profile in which text
files open with a test "opener" that only logs the path.

## File operations

`copy`, `cut`, `paste`, `trash`, `delete`, `rename_dialog`, `perform_drag_drop` and
`choose_popup_entry` change files or the clipboard. Whale refuses them unless it was started with
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
  of `perform_drag_drop`).
- Popup menus (context menu, drop menu) need real input on Wayland; they are only
  testable offscreen.
- Only the current tab's panes are addressed.
- The automation answers before Dolphin restores its session at start-up; wait for
  the restored state (e.g. `tabs.count`) before acting.
- Restart tests need a private session bus (`test_m6_acceptance.py` starts one with
  `dbus-run-session`): Dolphin restores its session only as the only instance.
- Without a window manager (offscreen) the main window is not activated again after
  a dialog closes; only the logical focus (`focus.role`) can be checked then.
