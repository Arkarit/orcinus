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
        "pluginTopItem (scroll position of a plugin item view), storedViewStates (views with a stored state at "
        "the location), restore (source history/session, requested and shown view, fallback), "
        "selection per pane (selection URLs, selectedNames, current/currentName: the host's; pluginSelection/"
        "pluginCurrent: what a plugin item view shows), statusText, shell.selectedNames (what the information "
        "panel got last), hidden files, sorting, filter, loading, recent pane activations, available view IDs.",
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
        "\"whale_testlist\" or \"whale_hello\" (see availableViews in the state).",
        "inputSchema": {
            "type": "object",
            "properties": {"pane": PANE, "view": {"type": "string"}},
            "required": ["view"],
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
        "enabled (view gating). copy, cut, paste, trash and delete change files or the clipboard and are refused "
        "unless Whale was started for a test profile with WHALE_DEV_MCP_FILE_OPERATIONS=1. rename opens Whale's "
        "rename path (inline in views that can, else the dialog: see rename_dialog); properties opens a dialog.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {
                    "type": "string",
                    "enum": ["focus_other_view", "go_up", "go_back", "go_forward", "edit_location", "select_all",
                             "invert_selection", "rename", "properties", "new_tab", "activate_next_tab",
                             "activate_prev_tab", "copy", "cut", "paste", "trash", "delete"],
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
        "name": "screenshot",
        "description": "PNG of the Whale main window, rendered by Whale itself (works on Wayland, also when the "
        "window is covered). Optionally also saved to a file.",
        "inputSchema": {"type": "object", "properties": {"save_path": {"type": "string"}}},
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
            return result(iface.SetView(pane, args["view"]))
        if tool == "activate_pane":
            return result(iface.ActivatePane(pane))
        if tool == "set_split_view":
            return result(iface.SetSplitView(bool(args["enabled"])))
        if tool == "set_location":
            return result(iface.SetLocation(pane, args["url"]))
        if tool == "set_filter_text":
            return result(iface.SetFilterText(pane, args["text"]))
        if tool == "clear_filter":
            return result(iface.ClearFilter(pane))
        if tool == "set_hidden_files":
            return result(iface.SetHiddenFiles(pane, bool(args["shown"])))
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
        if tool == "screenshot":
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
