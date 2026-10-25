#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""MCP server (stdio) for Whale's development automation.

A thin adapter: every tool is one call of the D-Bus object /whale/DevAutomation
that a Whale built with WHALE_ENABLE_DEV_MCP offers when started with
WHALE_DEV_MCP=1. The adapter holds no state and no logic of its own.

Usage:
  whale_dev_mcp.py                      run as MCP server on stdin/stdout
  whale_dev_mcp.py call TOOL [JSON]     call one tool and print the result (for shells)
  whale_dev_mcp.py tools                list the tools

Choosing the Whale instance: WHALE_DEV_MCP_PID=<pid> (or --pid) picks the instance with
that process ID; otherwise there must be exactly one instance with the automation.

Only the standard library and python3-dbus are used.
"""

import base64
import json
import os
import sys

import dbus

OBJECT_PATH = "/whale/DevAutomation"
INTERFACE = "io.github.arkarit.whale.DevAutomation"
PROTOCOL_VERSIONS = ["2025-06-18", "2025-03-26", "2024-11-05"]

PANE = {
    "description": "View area of the current tab: 0 or \"left\" (primary), 1 or \"right\" (secondary "
    "in split view), \"active\" (default).",
    "anyOf": [{"type": "integer", "minimum": 0}, {"type": "string", "enum": ["left", "right", "active"]}],
}

TOOLS = [
    {
        "name": "get_ui_state",
        "description": "Semantic UI state of Whale: panes, active pane, view per pane, visibility of the DolphinView, "
        "focus (semantic role, anomalies), URL, items (Dolphin model order) and rows of a plugin item view, "
        "pluginTopItem (scroll position of a plugin item view), pluginVisibleRows (first/last/count of the rows a "
        "plugin item view shows), pluginPreviewRows (rows that show a preview instead of an icon), previews (Dolphin's "
        "'Show Previews' at the location, the view's Previews capability, and the view's preview provider: enabled, "
        "pending, runningJobs, requested/generated/failed/canceled, jobsStarted/jobsKilled, maxPending), storedViewStates (views with a stored state at "
        "the location), restore (source history/session/preference/location, requested and shown view, fallback), "
        "preferredView (O3: the view remembered for the location, null if none), viewOrigin (what decided about "
        "the shown view: user, preference, history, session, fallback, kept, initial), "
        "selection per pane (selection URLs, selectedNames, current/currentName: the host's; pluginSelection/"
        "pluginCurrent: what a plugin item view shows), statusText, shell.selectedNames (what the information "
        "panel got last), hidden files, sorting, filter, loading, recent pane activations, available view IDs. "
        "TB6: toolbars.global (main toolbar: visible, layout, actions), toolbars.fileViewsVisible ('Show File View "
        "Toolbars'), toolbars.instances (the file views of all tabs: instanceId, tab, pane, visible, initialized), "
        "panes.N.toolbar (the pane's File View Toolbar: instanceId, ownerView, visible, shown, initialized, layout "
        "('-' separator, '|' spacer), actions with id, enabled, checked, shortcuts, actionKey (same key = same "
        "object), disabledReason, popup, menu), toolbarDialog (Configure Toolbars: open, selected, toolBars, "
        "available, current, modified, buttons). A File View Toolbar that has never been shown has no actions yet "
        "(initialized false); reading the state does not create them.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "get_focus_state",
        "description": "Only the focus part of the state: semantic role (e.g. dolphin-item-view, plugin:<id>, "
        "location-bar, filter-bar, search-field, places-panel, terminal-panel, hidden-dolphin-view), pane, "
        "active pane, widget class, windowActive, keyboardFocus (false while the window is not active: the role then "
        "names the widget that gets the focus on activation) and anomalies (focus in hidden DolphinView, focus in an "
        "inactive pane, focus proxy not pointing to the plugin).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "set_view",
        "description": "Shows a view in a pane: \"dolphin\" (the built-in file list) or a view plugin ID such as "
        "\"whale_treemap\", or the test views \"whale_testlist\"/\"whale_hello\" when Whale runs with the test "
        "plugin directory (see availableViews in the state). By default an automatic switch "
        "(nothing is remembered); remember=true is the user's choice of the view's View menu entry (for "
        "\"dolphin\": the entry of the file list's current mode; a mode entry chooses the file list only while "
        "another view is shown), which makes it the preferred view of the location (O3).",
        "inputSchema": {
            "type": "object",
            "properties": {"pane": PANE, "view": {"type": "string"}, "remember": {"type": "boolean"}},
            "required": ["view"],
        },
    },
    {
        "name": "get_preferred_view",
        "description": "O3: the view remembered for a location (URL or local path): view (\"dolphin\", a plugin ID or "
        "null), the normalized location key, and the number of remembered locations.",
        "inputSchema": {"type": "object", "properties": {"url": {"type": "string"}}, "required": ["url"]},
    },
    {
        "name": "set_preferred_view",
        "description": "O3: remembers a view for a location, as if chosen there in another window. Shown views do "
        "not change; it applies at the next navigation to the location.",
        "inputSchema": {
            "type": "object",
            "properties": {"url": {"type": "string"}, "view": {"type": "string"}},
            "required": ["url", "view"],
        },
    },
    {
        "name": "clear_preferred_view",
        "description": "O3: forgets the view remembered for a location (reset to default).",
        "inputSchema": {"type": "object", "properties": {"url": {"type": "string"}}, "required": ["url"]},
    },
    {
        "name": "set_view_plugin_available",
        "description": "Test hook: treats a view plugin as missing (available=false) or back (true) in the window's "
        "registry, as if it was uninstalled; shown views stay.",
        "inputSchema": {
            "type": "object",
            "properties": {"view": {"type": "string"}, "available": {"type": "boolean"}},
            "required": ["view", "available"],
        },
    },
    {
        "name": "activate_pane",
        "description": "Makes a pane the active view area (as clicking into it would).",
        "inputSchema": {"type": "object", "properties": {"pane": PANE}, "required": ["pane"]},
    },
    {
        "name": "set_split_view",
        "description": "Opens or closes the split view of the current tab (a setter, not a toggle). Closing "
        "closes the active side, like Whale's Close button.",
        "inputSchema": {"type": "object", "properties": {"enabled": {"type": "boolean"}}, "required": ["enabled"]},
    },
    {
        "name": "set_split_lock",
        "description": "Links or unlinks the two views of the split view (a setter, not a toggle) by "
        "clicking the lock button, as the user does. Fails while the button is hidden or disabled "
        "(the reason is in splitLock.toolTip of get_ui_state).",
        "inputSchema": {"type": "object", "properties": {"locked": {"type": "boolean"}}, "required": ["locked"]},
    },
    {
        "name": "apply_style",
        "description": "ST3: applies a style (its id, see styles.list of get_ui_state) through its action, "
        "as its entry in View > Styles or its shortcut does. The menu bar follows a moment later.",
        "inputSchema": {"type": "object", "properties": {"id": {"type": "string"}}, "required": ["id"]},
    },
    {
        "name": "save_style",
        "description": "ST3: saves the current state of the window as a style, as View > Styles > Save as Style... "
        "does without its dialog. A style of the same name (ignoring case) is captured again and keeps its id "
        "and shortcut. Without a name: View > Styles > Save Style, the style applied or saved last "
        "(styles.lastStyle). Returns the id.",
        "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}}},
    },
    {
        "name": "set_window_size",
        "description": "Resizes the Whale window as the user does (the compositor may adjust it); "
        "window.width/height in get_ui_state show the result.",
        "inputSchema": {
            "type": "object",
            "properties": {"width": {"type": "integer"}, "height": {"type": "integer"}},
            "required": ["width", "height"],
        },
    },
    {
        "name": "set_split_ratio",
        "description": "Moves the handle between the halves of the split view as the user drags it: the left "
        "half gets left_share (0.1 to 0.9) of the width.",
        "inputSchema": {"type": "object", "properties": {"left_share": {"type": "number"}}, "required": ["left_share"]},
    },
    {
        "name": "set_location",
        "description": "Navigates a pane to a URL or local path through Whale's location bar (history, focus "
        "and plugin views behave as with user navigation).",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "url": {"type": "string"}}, "required": ["url"]},
    },
    {
        "name": "set_filter_text",
        "description": "Opens the filter bar of a pane (it takes the focus) and sets its text.",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "text": {"type": "string"}}, "required": ["text"]},
    },
    {
        "name": "clear_filter",
        "description": "Closes the filter bar of a pane, which clears the filter and returns the focus to the view.",
        "inputSchema": {"type": "object", "properties": {"pane": PANE}},
    },
    {
        "name": "set_hidden_files",
        "description": "Shows or hides hidden files in a pane (a setter, not a toggle).",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "shown": {"type": "boolean"}}, "required": ["shown"]},
    },
    {
        "name": "set_content_scope",
        "description": "F1: the content scope of a pane's file list, through \"Show flat (no folders)\" as the user "
        "chooses it (the pane becomes active; the choice is remembered for the location): \"recursive-files\" "
        "(the files below the location, no folders) or \"current-folder\". Only for the file list at a local "
        "folder. See panes.N.contentScope in the state.",
        "inputSchema": {
            "type": "object",
            "properties": {"pane": PANE, "scope": {"type": "string", "enum": ["current-folder", "recursive-files"]}},
            "required": ["scope"],
        },
    },
    {
        "name": "set_sort",
        "description": "Sets the sorting of a pane. Omitted values stay unchanged. Note that Whale switches to the "
        "preferred order of a field when the field changes; the order given here is applied after that.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pane": PANE,
                "field": {"type": "string", "enum": ["name", "size", "modified", "created", "type", "extension"]},
                "order": {"type": "string", "enum": ["ascending", "descending"]},
                "folders_first": {"type": "boolean"},
            },
        },
    },
    {
        "name": "set_selection",
        "description": "Sets the selection of a pane through the host API that plugin views use (the DolphinView "
        "stays the store). Only items of the pane's listing; others are returned as ignored. 'current' (optional) "
        "sets the current item, otherwise it stays.",
        "inputSchema": {
            "type": "object",
            "properties": {"pane": PANE, "urls": {"type": "array", "items": {"type": "string"}},
                           "current": {"type": "string"}},
            "required": ["urls"],
        },
    },
    {
        "name": "activate_items",
        "description": "Opens items of a pane like Enter or a double click, through the host API that plugin "
        "views use: folders are entered, files open with their default application (careful: real applications "
        "unless the profile maps the type to a test opener).",
        "inputSchema": {
            "type": "object",
            "properties": {"pane": PANE, "urls": {"type": "array", "items": {"type": "string"}, "minItems": 1}},
            "required": ["urls"],
        },
    },
    {
        "name": "trigger_action",
        "description": "Triggers one of a fixed set of Whale actions on the active pane, only if Whale has it "
        "enabled (view gating), on what the menus, toolbar and shortcuts act on (T2: a plugin view's own selection, "
        "see 'command' in the state). copy, cut, paste, trash, delete, duplicate, copy_location, copy_to_other_pane "
        "and move_to_other_pane change files or the clipboard and are refused "
        "unless Whale was started for a test profile with WHALE_DEV_MCP_FILE_OPERATIONS=1. rename opens Whale's "
        "rename path (inline in views that can, else the dialog: see rename_dialog); properties opens a dialog; "
        "show_preview toggles 'Show Previews' of the location; view_icons/view_compact/view_details choose the "
        "file list's view mode.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {
                    "type": "string",
                    "enum": ["focus_other_view", "go_up", "go_back", "go_forward", "edit_location", "select_all",
                             "invert_selection", "rename", "properties", "new_tab", "close_tab",
                             "whale_show_file_view_toolbars", "activate_next_tab",
                             "activate_prev_tab", "show_preview", "reload", "show_information_panel", "view_icons",
                             "view_compact", "view_details", "copy", "cut",
                             "paste", "trash", "delete", "duplicate", "copy_location", "copy_to_other_pane",
                             "move_to_other_pane"],
                }
            },
            "required": ["action"],
        },
    },
    {
        "name": "get_action_state",
        "description": "Enabled state of Whale's file and view actions and why the view gating disables them "
        "(gate: null if not gated), keyed by Whale's action names (e.g. edit_copy, movetotrash, renamefile, "
        "view_zoom_in, sort, toggle_selection_mode). Also part of get_ui_state as 'actions'.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "open_context_menu",
        "description": "Opens Whale's context menu of a pane through the host API that plugin views use: for an "
        "item of the listing (url; it becomes the selection if it is not selected) or, without url, for the "
        "location. Returns at once; the menu shows up as popupMenu in get_ui_state until close_popup.",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "url": {"type": "string"}}},
    },
    {
        "name": "scroll_to_item",
        "description": "Scrolls a plugin item view (e.g. whale_testlist) so that the item url is at its top; "
        "panes.N.pluginTopItem in get_ui_state shows it. A view state that the view saves and restores.",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "url": {"type": "string"}}, "required": ["url"]},
    },
    {
        "name": "quit",
        "description": "Closes the Whale window like the user does: Whale saves its session (with 'remember open "
        "tabs') and quits. Start it again in the same profile (shell) to test the session restore.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "close_popup",
        "description": "Closes open popup menus (e.g. a context menu) without choosing an entry.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "request_item_info",
        "description": "Reports an item of a pane as hovered (or none without url) through the host API that "
        "plugin views use; the information panel follows. See shell.itemInfo in get_ui_state.",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "url": {"type": "string"}}},
    },
    {
        "name": "rename_dialog",
        "description": "Enters a new name in the open rename dialog and confirms it (the dialog that "
        "trigger_action rename opens). Needs WHALE_DEV_MCP_FILE_OPERATIONS=1. Open dialogs are listed as "
        "'dialogs' in get_ui_state.",
        "inputSchema": {"type": "object", "properties": {"new_name": {"type": "string"}}, "required": ["new_name"]},
    },
    {
        "name": "close_dialogs",
        "description": "Cancels all open dialogs (rename, properties, ...).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "wait_for_state",
        "description": "Waits inside Whale until all conditions hold or the timeout ends; use it instead of sleeps. "
        "Conditions map a dotted path into get_ui_state (array indexes as numbers) to the expected JSON value, e.g. "
        "{\"panes.1.viewId\": \"whale_testlist\", \"focus.role\": \"plugin:whale_testlist\", \"panes.0.loading\": false}. "
        "Instead of a value, {\"$contains\": x} matches a string containing x or an array containing x. "
        "Returns matched, timedOut, mismatches (path, expected, actual), elapsedMs and the final state.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "conditions": {"type": "object"},
                "timeout_ms": {"type": "integer", "minimum": 0, "maximum": 60000, "default": 5000},
            },
            "required": ["conditions"],
        },
    },
    {
        "name": "get_preview_state",
        "description": "Preview state of all panes (as 'previews' in get_ui_state) and the answers to request_preview "
        "(results: pane, name, preview true/false, width, height).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "request_preview",
        "description": "Requests previews of items of a pane (urls) through the preview provider of the pane's plugin "
        "view, as the view does; the answers show up in get_preview_state as results. Nothing is requested while the "
        "provider is disabled (previews off, or the view lacks the Previews capability).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pane": PANE,
                "urls": {"type": "array", "items": {"type": "string"}},
                "size": {"type": "integer", "minimum": 16, "maximum": 512, "default": 128},
            },
            "required": ["urls"],
        },
    },
    {
        "name": "perform_drag_drop",
        "description": "Drags the selection of source_pane and drops it onto target_url (a folder item of the target "
        "pane; without it the location) in target_pane. action: copy (Ctrl), move (Shift), link (Ctrl+Shift) or ask "
        "(no modifier: KDE's drop menu opens, see popupMenu and choose_popup_entry). A plugin item view gets the real "
        "drag gesture and starts its drag through Whale's file operations; the file list's drag data comes from its "
        "own drag function. The target view gets drag enter/move/drop events and handles them itself (same code as a "
        "real drop); only the transport (QDrag::exec) is skipped. Changes files: needs "
        "WHALE_DEV_MCP_FILE_OPERATIONS=1 (throwaway test profiles only).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "source_pane": PANE,
                "target_pane": PANE,
                "target_url": {"type": "string"},
                "action": {"type": "string", "enum": ["copy", "move", "link", "ask"]},
            },
            "required": ["source_pane", "target_pane", "action"],
        },
    },
    {
        "name": "choose_popup_entry",
        "description": "Chooses an entry of the open popup menu by its text (without '&'; the shortcut after a tab "
        "may be left out), e.g. 'Copy Here' in the drop menu. Needs WHALE_DEV_MCP_FILE_OPERATIONS=1, as menu entries may change files.",
        "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"]},
    },
    {
        "name": "treemap_hover",
        "description": "Moves the pointer onto the cell of url in the treemap view (whale_treemap) with a real mouse "
        "event, or out of the treemap without url. The treemap outlines it and reports it to the information panel "
        "(shell.itemInfo*). State: panes.N.treemap in get_ui_state.",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "url": {"type": "string"}}},
    },
    {
        "name": "treemap_select",
        "description": "Selects url in the treemap as the user does: a click into its cell (which hits the deepest "
        "entry there), then the Up key until url itself is selected. Deep entries stay the view's own selection.",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "url": {"type": "string"}}, "required": ["url"]},
    },
    {
        "name": "treemap_activate",
        "description": "Opens a folder from the treemap as the new location. how=enter (default): treemap_select, "
        "then Enter (opens the selected folder, or the folder of a selected file). how=double_click: a double click "
        "at the center of the cell of url (opens the folder of the deepest entry there).",
        "inputSchema": {
            "type": "object",
            "properties": {"pane": PANE, "url": {"type": "string"}, "how": {"type": "string", "enum": ["enter", "double_click"]}},
            "required": ["url"],
        },
    },
    {
        "name": "treemap_zoom",
        "description": "Zooms within the treemap with the mouse wheel: direction=in at the cell of url (or the "
        "center) zooms one folder level towards it, out zooms out one level. The location does not change.",
        "inputSchema": {
            "type": "object",
            "properties": {"pane": PANE, "direction": {"type": "string", "enum": ["in", "out"]}, "url": {"type": "string"}},
            "required": ["direction"],
        },
    },
    {
        "name": "treemap_key",
        "description": "Presses a key in the treemap (focuses it first), e.g. Left/Right/Up/Down, Return, +, -, "
        "Escape, or a shortcut such as Delete or Ctrl+C: the window's shortcuts get it first, as for real input "
        "(result.shortcut tells whether one took it).",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "key": {"type": "string"}}, "required": ["key"]},
    },
    {
        "name": "treemap_context_menu",
        "description": "Right-clicks the cell of url in the treemap with real mouse input: the deepest entry "
        "there becomes the selection and the treemap's own context menu opens (popupMenu in the state; "
        "result.menuShown). For a folder covered by its entries use treemap_select and treemap_key Menu. "
        "Popup menus need real input on Wayland.",
        "inputSchema": {"type": "object", "properties": {"pane": PANE, "url": {"type": "string"}}, "required": ["url"]},
    },
    {
        "name": "file_operation",
        "description": "T1: carries out an operation on explicit items (also deep below the location) through "
        "the file operations of the pane's plugin view, as its own context menu does: open, open_with, "
        "show_in_folder, properties, rename (dialog), trash, delete, copy, cut, copy_location; supported only "
        "reports what is possible. rename/trash/delete/copy/cut/copy_location need WHALE_DEV_MCP_FILE_OPERATIONS=1. "
        "Returns at once; panes.N.fileOperations.last reports the end of rename/trash/delete.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pane": PANE,
                "operation": {"type": "string", "enum": ["open", "open_with", "show_in_folder", "properties", "rename",
                                                         "trash", "delete", "copy", "cut", "copy_location", "supported"]},
                "urls": {"type": "array", "items": {"type": "string"}},
            },
            "required": ["operation", "urls"],
        },
    },
    {
        "name": "answer_dialog",
        "description": "Presses a button of an open dialog by its text (without '&'), e.g. 'Delete' in KIO's "
        "confirmation. The open dialogs, their texts and buttons are 'dialogs' in the state. "
        "Needs WHALE_DEV_MCP_FILE_OPERATIONS=1.",
        "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"]},
    },
    {
        "name": "treemap_set_options",
        "description": "Sets the treemap's display options with its own controls.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pane": PANE,
                "color_mode": {"type": "string", "enum": ["fileType", "topFolder"]},
                "labels": {"type": "boolean"},
            },
        },
    },
    {
        "name": "trigger_toolbar_action",
        "description": "Clicks a button of a toolbar with mouse events through the window, as the user does (TB6): "
        "toolbar 'global' (the main toolbar, acting on the active view), a pane index of the current tab, "
        "'active', or the instanceId of a file view (panes.N.toolbar.instanceId, any tab). A press on a File View "
        "Toolbar makes its view the active one. id is Whale's action name (go_back, view_settings, show_preview ...). "
        "With menu_entry the button's menu is opened as the user does and that entry is chosen (its id or text, "
        "'Submenu > Entry' for submenus, e.g. 'sort > sort_by_size' or a history entry's text). Fails for an unknown "
        "toolbar, instanceId or action, an action that is not in the toolbar or disabled (with the reason), a "
        "missing entry, a hidden or never shown toolbar; actions that change files need "
        "WHALE_DEV_MCP_FILE_OPERATIONS=1.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "toolbar": {"oneOf": [{"type": "string"}, {"type": "integer"}]},
                "id": {"type": "string"},
                "menu_entry": {"type": "string"},
            },
            "required": ["toolbar", "id"],
        },
    },
    {
        "name": "location_bar_input",
        "description": "Enters text into the location bar of a file view as the user does (location bar plan, LB4): "
        "a click on its toggle button if it shows the breadcrumb, the text typed over the editor's text, and with "
        "submit Return (the view then navigates there). toolbar is a pane index of the current tab, 'active', or "
        "the instanceId of a file view (panes.N.toolbar.instanceId). The location bar's state is "
        "panes.N.toolbar.locationBar. Ctrl+L is trigger_action edit_location. Fails for an unknown pane or "
        "instanceId, and if the location bar is not in the File View Toolbar or not shown.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "toolbar": {"oneOf": [{"type": "string"}, {"type": "integer"}]},
                "text": {"type": "string"},
                "submit": {"type": "boolean"},
            },
            "required": ["toolbar", "text"],
        },
    },
    {
        "name": "configure_toolbars",
        "description": "Operates Configure Toolbars through its own widgets, as the user does (TB6), in this order: "
        "open (true; with select the toolbar chosen when it opens), select ('global' or 'file-view'), remove and "
        "insert (action ids or a list of them; '-' separator, '|' spacer; inserted at the end), defaults (KDE's "
        "Defaults, its question answered with Reset: resets all toolbars), apply, ok, cancel. Returns the dialog's "
        "state (also toolbarDialog in get_ui_state).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "open": {"type": "boolean"},
                "select": {"type": "string", "enum": ["global", "file-view"]},
                "insert": {"oneOf": [{"type": "string"}, {"type": "array", "items": {"type": "string"}}]},
                "remove": {"oneOf": [{"type": "string"}, {"type": "array", "items": {"type": "string"}}]},
                "defaults": {"type": "boolean"},
                "apply": {"type": "boolean"},
                "ok": {"type": "boolean"},
                "cancel": {"type": "boolean"},
            },
        },
    },
    {
        "name": "viewer_input",
        "description": "V1d: input for the built-in viewer (opened with activate_items on an image), sent to its "
        "window as real events: action key (key as in QKeySequence: Right/Left/Up/Down, PgDown/PgUp, Home/End, "
        "Escape closes), wheel (delta: 120 a notch, negative = down = next file; fast turns add up) or click "
        "(where: left/right fifth page, center does nothing). V2e: keys +, -, 1 (100 %), 0 (fit) zoom; "
        "zoom_wheel (Ctrl+wheel: delta, positive = in; at: [x, y] fractions of the window, default the middle) "
        "and drag (dx, dy: the middle button drags a zoomed image from the middle of the window). The state is "
        "viewer in get_ui_state.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string", "enum": ["key", "wheel", "click", "zoom_wheel", "drag"]},
                "key": {"type": "string"},
                "delta": {"type": "integer"},
                "where": {"type": "string", "enum": ["left", "center", "right"]},
                "at": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
                "dx": {"type": "integer"},
                "dy": {"type": "integer"},
            },
            "required": ["action"],
        },
    },
    {
        "name": "set_viewer_options",
        "description": "V1d: the settings of the built-in viewer, as its settings page writes them: wrap_around, "
        "mime_types (e.g. [\"image/*\"] or single types; [] opens nothing in the viewer) and the hidden "
        "multi_selection (keep: paging through a multiple selection keeps it, move: the shown file becomes the "
        "selection); V2e: upscale (small images fill the screen) and smooth_zoom (smooth instead of sharp pixels "
        "beyond 100 %), read when the viewer opens. Only the given ones change; viewer.settings in get_ui_state.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "wrap_around": {"type": "boolean"},
                "upscale": {"type": "boolean"},
                "smooth_zoom": {"type": "boolean"},
                "mime_types": {"type": "array", "items": {"type": "string"}},
                "multi_selection": {"type": "string", "enum": ["keep", "move"]},
            },
        },
    },
    {
        "name": "screenshot",
        "description": "PNG of the Whale main window (target window, default) or of the open built-in viewer "
        "(target viewer), rendered by Whale itself (works on Wayland, also when the window is covered). "
        "Optionally also saved to a file.",
        "inputSchema": {
            "type": "object",
            "properties": {"save_path": {"type": "string"}, "target": {"type": "string", "enum": ["window", "viewer"]}},
        },
    },
]


class ToolError(Exception):
    pass


def pane_index(value):
    if value is None or value == "active":
        return -1
    if value == "left":
        return 0
    if value == "right":
        return 1
    if isinstance(value, int) and value >= 0:
        return value
    raise ToolError(f"invalid pane {value!r}")


def find_instance(bus, pid=None):
    """Returns the bus name of the Whale instance that offers the automation."""
    names = [str(n) for n in bus.list_names() if str(n).startswith("io.github.arkarit.whale")]
    driver = bus.get_object("org.freedesktop.DBus", "/org/freedesktop/DBus")
    found = []
    for name in names:
        try:
            # The bus daemon answers this one; a busy Whale cannot delay it.
            owner_pid = int(driver.GetConnectionUnixProcessID(name, dbus_interface="org.freedesktop.DBus"))
            if pid is not None and owner_pid != pid:
                continue
            # A Whale that is still starting may not answer yet, so do not wait long.
            xml = bus.call_blocking(name, OBJECT_PATH, "org.freedesktop.DBus.Introspectable", "Introspect", "", (),
                                    timeout=2)
        except dbus.DBusException:
            continue
        if INTERFACE in str(xml):
            found.append((name, owner_pid))
    if len(found) == 1:
        return found[0][0]
    if not found:
        raise ToolError("no Whale instance with the development automation found; start a Whale built with "
                        "WHALE_ENABLE_DEV_MCP=ON with WHALE_DEV_MCP=1" + (f" (pid {pid})" if pid else ""))
    raise ToolError("several Whale instances offer the automation, choose one with WHALE_DEV_MCP_PID: "
                    + ", ".join(f"pid {p}" for _, p in found))


class Whale:
    def __init__(self, pid=None):
        self.pid = pid
        self.bus = dbus.SessionBus()

    def iface(self):
        # Looked up on every call, so a restarted Whale is found again.
        name = find_instance(self.bus, self.pid)
        return dbus.Interface(self.bus.get_object(name, OBJECT_PATH, introspect=False), INTERFACE)

    def call(self, tool, args):
        """Returns a list of MCP content items."""
        iface = self.iface()

        def result(text):
            data = json.loads(str(text))
            if isinstance(data, dict) and data.get("ok") is False:
                raise ToolError(data.get("error", "failed"))
            return [{"type": "text", "text": json.dumps(data, indent=1)}]

        pane = pane_index(args.get("pane"))
        if tool == "get_ui_state":
            return result(iface.UiState())
        if tool == "get_focus_state":
            return result(iface.FocusState())
        if tool == "set_view":
            return result(iface.SetView(pane, args["view"], bool(args.get("remember", False))))
        if tool == "get_preferred_view":
            return result(iface.PreferredView(args["url"]))
        if tool == "set_preferred_view":
            return result(iface.SetPreferredView(args["url"], args["view"]))
        if tool == "clear_preferred_view":
            return result(iface.SetPreferredView(args["url"], ""))
        if tool == "set_view_plugin_available":
            return result(iface.SetViewPluginAvailable(args["view"], bool(args["available"])))
        if tool == "activate_pane":
            return result(iface.ActivatePane(pane))
        if tool == "set_window_size":
            return result(iface.SetWindowSize(int(args["width"]), int(args["height"])))
        if tool == "set_split_ratio":
            return result(iface.SetSplitRatio(float(args["left_share"])))
        if tool == "set_split_view":
            return result(iface.SetSplitView(bool(args["enabled"])))
        if tool == "set_split_lock":
            return result(iface.SetSplitLock(bool(args["locked"])))
        if tool == "apply_style":
            return result(iface.ApplyStyle(args["id"]))
        if tool == "save_style":
            return result(iface.SaveStyle(args.get("name", "")))
        if tool == "set_location":
            return result(iface.SetLocation(pane, args["url"]))
        if tool == "set_filter_text":
            return result(iface.SetFilterText(pane, args["text"]))
        if tool == "clear_filter":
            return result(iface.ClearFilter(pane))
        if tool == "set_hidden_files":
            return result(iface.SetHiddenFiles(pane, bool(args["shown"])))
        if tool == "set_content_scope":
            return result(iface.SetContentScope(pane, args["scope"]))
        if tool == "set_sort":
            folders = args.get("folders_first")
            folders = "" if folders is None else ("true" if folders else "false")
            return result(iface.SetSort(pane, args.get("field", ""), args.get("order", ""), folders))
        if tool == "set_selection":
            urls = dbus.Array([str(url) for url in args["urls"]], signature="s")
            return result(iface.SetSelection(pane, urls, args.get("current", "")))
        if tool == "activate_items":
            urls = dbus.Array([str(url) for url in args["urls"]], signature="s")
            return result(iface.ActivateItems(pane, urls))
        if tool == "get_action_state":
            return result(iface.ActionState())
        if tool == "open_context_menu":
            return result(iface.OpenContextMenu(pane, args.get("url", "")))
        if tool == "scroll_to_item":
            return result(iface.ScrollToItem(pane, args["url"]))
        if tool == "quit":
            return result(iface.Quit())
        if tool == "close_popup":
            return result(iface.ClosePopups())
        if tool == "request_item_info":
            return result(iface.RequestItemInfo(pane, args.get("url", "")))
        if tool == "rename_dialog":
            return result(iface.AnswerRenameDialog(args["new_name"]))
        if tool == "close_dialogs":
            return result(iface.CloseDialogs())
        if tool == "trigger_action":
            return result(iface.TriggerAction(args["action"]))
        if tool == "wait_for_state":
            timeout = int(args.get("timeout_ms", 5000))
            text = iface.WaitForState(json.dumps(args["conditions"]), timeout, timeout=timeout / 1000 + 10)
            return result(text)
        if tool == "get_preview_state":
            return result(iface.PreviewState())
        if tool == "request_preview":
            urls = dbus.Array([str(url) for url in args["urls"]], signature="s")
            return result(iface.RequestPreview(pane, urls, int(args.get("size", 128))))
        if tool == "perform_drag_drop":
            return result(iface.PerformDragDrop(pane_index(args.get("source_pane")), pane_index(args.get("target_pane")),
                                                args.get("target_url", ""), args["action"]))
        if tool == "choose_popup_entry":
            return result(iface.ChoosePopupEntry(args["text"]))
        if tool == "treemap_hover":
            return result(iface.TreemapInput(pane, "hover", args.get("url", ""), ""))
        if tool == "treemap_select":
            return result(iface.TreemapInput(pane, "select", args["url"], ""))
        if tool == "treemap_activate":
            how = args.get("how", "enter")
            return result(iface.TreemapInput(pane, "activate" if how == "enter" else "double_click", args["url"], ""))
        if tool == "treemap_zoom":
            return result(iface.TreemapInput(pane, "wheel_" + args["direction"], args.get("url", ""), ""))
        if tool == "treemap_key":
            return result(iface.TreemapInput(pane, "key", "", args["key"]))
        if tool == "treemap_context_menu":
            return result(iface.TreemapInput(pane, "context_menu", args["url"], ""))
        if tool == "file_operation":
            urls = dbus.Array([str(url) for url in args["urls"]], signature="s")
            return result(iface.FileOperation(pane, args["operation"], urls))
        if tool == "answer_dialog":
            return result(iface.AnswerDialog(args["text"]))
        if tool == "treemap_set_options":
            labels = args.get("labels")
            return result(iface.TreemapSetOptions(pane, args.get("color_mode", ""), "" if labels is None else ("true" if labels else "false")))
        if tool == "trigger_toolbar_action":
            return result(iface.TriggerToolbarAction(str(args["toolbar"]), args["id"], args.get("menu_entry", "")))
        if tool == "location_bar_input":
            return result(iface.LocationBarInput(str(args["toolbar"]), args["text"], bool(args.get("submit", False))))
        if tool == "configure_toolbars":
            return result(iface.ConfigureToolbars(json.dumps(args)))
        if tool == "viewer_input":
            action = args["action"]
            at = args.get("at")
            argument = {"key": args.get("key", ""), "wheel": str(args.get("delta", "")),
                        "click": args.get("where", ""),
                        "zoom_wheel": " ".join(str(v) for v in [args.get("delta", "")] + (list(at) if at else [])),
                        "drag": f"{args.get('dx', '')} {args.get('dy', '')}"}.get(action, "")
            return result(iface.ViewerInput(action, argument))
        if tool == "set_viewer_options":
            names = {"wrap_around": "wrapAround", "mime_types": "mimeTypes", "multi_selection": "multiSelection",
                     "upscale": "upscale", "smooth_zoom": "smoothZoom"}
            options = {names[key]: value for key, value in args.items() if key in names}
            return result(iface.SetViewerOptions(json.dumps(options)))
        if tool == "screenshot":
            if args.get("target", "window") == "viewer":
                png = bytes(iface.ViewerScreenshot(byte_arrays=True))
                if not png:
                    raise ToolError("the viewer is not open")
            else:
                png = bytes(iface.Screenshot(byte_arrays=True))
            path = args.get("save_path")
            if path:
                with open(path, "wb") as f:
                    f.write(png)
            return [{"type": "image", "data": base64.b64encode(png).decode(), "mimeType": "image/png"}]
        raise ToolError(f"unknown tool {tool!r}")


def serve(whale):
    def send(message):
        sys.stdout.write(json.dumps(message) + "\n")
        sys.stdout.flush()

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
        except json.JSONDecodeError:
            send({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}})
            continue
        method = request.get("method")
        rid = request.get("id")
        if rid is None:
            continue  # notifications, e.g. notifications/initialized
        params = request.get("params") or {}
        if method == "initialize":
            requested = params.get("protocolVersion")
            send({"jsonrpc": "2.0", "id": rid, "result": {
                "protocolVersion": requested if requested in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0],
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "whale-dev", "version": "0.1"},
                "instructions": "Development and UI test automation of a running Whale (dev build, "
                                "WHALE_DEV_MCP=1). Use wait_for_state instead of sleeps.",
            }})
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": rid, "result": {}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": rid, "result": {"tools": TOOLS}})
        elif method == "tools/call":
            try:
                content = whale.call(params.get("name"), params.get("arguments") or {})
                send({"jsonrpc": "2.0", "id": rid, "result": {"content": content, "isError": False}})
            except (ToolError, dbus.DBusException, KeyError) as error:
                message = f"missing argument {error}" if isinstance(error, KeyError) else str(error)
                send({"jsonrpc": "2.0", "id": rid, "result": {
                    "content": [{"type": "text", "text": message}], "isError": True}})
        else:
            send({"jsonrpc": "2.0", "id": rid, "error": {"code": -32601, "message": f"unknown method {method}"}})


def main(argv):
    pid = os.environ.get("WHALE_DEV_MCP_PID")
    if "--pid" in argv:
        index = argv.index("--pid")
        pid = argv[index + 1]
        del argv[index:index + 2]
    whale = Whale(int(pid) if pid else None)

    if len(argv) >= 2 and argv[1] == "tools":
        print("\n".join(tool["name"] for tool in TOOLS))
        return 0
    if len(argv) >= 3 and argv[1] == "call":
        args = json.loads(argv[3]) if len(argv) > 3 else {}
        try:
            content = whale.call(argv[2], args)
        except (ToolError, dbus.DBusException) as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        for item in content:
            if item["type"] == "text":
                print(item["text"])
            else:
                print(f"<{item['mimeType']}, {len(base64.b64decode(item['data']))} bytes>")
        return 0
    serve(whale)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
