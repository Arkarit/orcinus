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
| Tests | `tests/` | In-process test of the automation, end-to-end M3 acceptance through the MCP. |

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
| `set_split_view` | Open/close split view (setter) |
| `set_location` | Navigate a pane through the location bar |
| `set_filter_text` / `clear_filter` | Filter bar of a pane |
| `set_hidden_files` | Setter, not a toggle |
| `set_sort` | `field` (name, size, modified, created, type, extension), `order`, `folders_first` |
| `trigger_action` | Only `focus_other_view`, `go_up`, `go_back`, `go_forward`, `edit_location`, `select_all`, `invert_selection`, `rename`. No file operations. |
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
    "selectionCount": 0, "selection": [],
    "hiddenFiles": false, "sort": {"field": "name", "order": "ascending", "foldersFirst": true},
    "filter": {"text": "", "barVisible": false},
    "focusProxy": "plugin-widget", "hiddenDolphinViewHasFocus": false
  }],
  "focus": {"role": "plugin:whale_testlist", "pane": 0, "activePane": 0, "widgetClass": "QListView",
            "windowActive": true, "keyboardFocus": true, "anomalies": []},
  "activations": {"count": 3, "recent": [{"seq": 3, "tab": 0, "pane": 0, "focusRole": "…"}]},
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
- `activations` counts pane activations, so a test can prove that an operation did
  not activate the other half of a split view.

`wait_for_state` conditions map dotted paths into this state to expected JSON
values, e.g. `{"panes.1.viewId": "whale_testlist", "focus.role": "plugin:whale_testlist"}`.

## Limits

- The state is semantic and immediate; pixels may lag behind: a screenshot taken
  right after a state change can still show an animation (filter bar, split view).
- Navigation, view switching and pane activation go through the same functions as
  the UI, but not through simulated mouse or keyboard input.
- Only the current tab's panes are addressed.
