#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the view plugin milestone M3, driven only through the dev MCP.

Starts a real Whale (own profile, own test folder) with WHALE_DEV_MCP=1, talks to it
through the MCP adapter over stdio exactly like an MCP client, and walks through the
M3 acceptance list: test list view, navigation, sorting, hidden files, name filter,
split view with Dolphin and the test list, pane activation, focus, view switching.
A second Whale without WHALE_DEV_MCP must not offer the automation.

  test_m3_acceptance.py --whale BIN --adapter ADAPTER --plugin-dir DIR
                        [--platform offscreen|wayland|xcb] [--screenshots DIR]

Exit code 77 means "skipped" (no D-Bus session bus).
"""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

# Never offered (Whale's own names); file operations that are offered (copy, trash, duplicate, ...) are
# refused without a test profile.
FORBIDDEN_ACTIONS = {"move_to_trash", "deletefile", "edit_paste", "create_dir", "compress"}


sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance


def run(args, work):
    folder = work / "files"
    (folder / "sub" / "deeper").mkdir(parents=True)
    (folder / "a.txt").write_bytes(b"a" * 300)
    (folder / "b.txt").write_bytes(b"b" * 100)
    (folder / "c.txt").write_bytes(b"c" * 200)
    (folder / ".hidden").write_bytes(b"h")
    (folder / "sub" / "inner.txt").write_bytes(b"i")
    folder_url = folder.as_uri()
    sub_url = (folder / "sub").as_uri()

    whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True)
    plain = None
    env = dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid))
    client = McpClient(args.adapter, env)
    try:
        init = client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                             "clientInfo": {"name": "test_m3_acceptance", "version": "1"}})
        assert init["serverInfo"]["name"] == "whale-dev", init
        client.notify("notifications/initialized")
        tools = {tool["name"]: tool for tool in client.request("tools/list")["tools"]}
        offered = set(tools["trigger_action"]["inputSchema"]["properties"]["action"]["enum"])
        if offered & FORBIDDEN_ACTIONS:
            raise Failure(f"destructive actions offered: {offered & FORBIDDEN_ACTIONS}")
        # Exactly this small set; anything new must be a deliberate decision (see README.md).
        expected_tools = {"get_ui_state", "get_focus_state", "set_view", "activate_pane", "set_split_view",
                          "set_location", "set_filter_text", "clear_filter", "set_hidden_files", "set_content_scope", "set_sort",
                          "set_selection", "activate_items", "get_action_state", "open_context_menu", "close_popup",
                          "request_item_info", "rename_dialog", "close_dialogs", "scroll_to_item", "quit",
                          "trigger_action", "wait_for_state", "screenshot", "get_preview_state", "request_preview",
                          "perform_drag_drop", "choose_popup_entry", "set_split_ratio", "set_split_lock", "set_window_size",
                          "treemap_hover", "treemap_select", "treemap_activate", "treemap_zoom", "treemap_key",
                          "treemap_set_options", "treemap_context_menu", "file_operation", "answer_dialog",
                          "get_preferred_view", "set_preferred_view", "clear_preferred_view",
                          "set_view_plugin_available", "trigger_toolbar_action", "configure_toolbars",
                          "location_bar_input", "apply_style", "save_style", "viewer_input",
                          "set_viewer_options"}
        if set(tools) != expected_tools:
            raise Failure(f"unexpected tool set: {sorted(set(tools) ^ expected_tools)}")
        wait_for_instance(client, whale)
        print("ok    MCP initialized, tools:", ", ".join(sorted(tools)))

        # Runtime switch: a Whale started without WHALE_DEV_MCP offers nothing. Started only now:
        # two Whales starting at the same moment wait for each other on D-Bus (up to 25 s each),
        # because a starting Whale asks all running instances for their windows.
        # Always offscreen: on a real desktop it would take the activation from the Whale under test.
        plain = start_whale(args.whale, folder, work / "profile-plain", args.plugin_dir, "offscreen", automation=False)
        if json.loads(client.tool("get_ui_state")[0]["text"])["pid"] != whale.pid:
            raise Failure("the adapter talks to the wrong Whale")
        plain_ready = subprocess.run(["gdbus", "wait", "--session", "--timeout", "30", f"io.github.arkarit.orcinus-{plain.pid}"],
                                     capture_output=True)
        if plain_ready.returncode != 0:
            raise Failure("the Whale without automation did not start")
        gate = subprocess.run([sys.executable, args.adapter, "--pid", str(plain.pid), "call", "get_ui_state"],
                              capture_output=True, text=True, env=dict(os.environ))
        if gate.returncode == 0 or "no Whale instance" not in gate.stderr:
            raise Failure(f"Whale without WHALE_DEV_MCP answered: {gate.stdout} {gate.stderr}")
        print("ok    Whale without WHALE_DEV_MCP offers no automation")
        for tool, arguments in (("trigger_action", {"action": "delete"}), ("trigger_action", {"action": "paste"})):
            if client.tool_error(tool, **arguments) is None:
                raise Failure(f"{tool}{arguments} was accepted")
        if "WHALE_DEV_MCP_FILE_OPERATIONS" not in (client.tool_error("trigger_action", action="trash") or ""):
            raise Failure("trash was not refused for lack of a test profile")
        print("ok    file operations are refused without a test profile")

        s = Scenario(client, args.screenshots)
        # Start from defaults; Whale shares the view properties of all folders.
        client.tool("set_hidden_files", shown=False)
        client.tool("set_sort", field="name", order="ascending", folders_first=True)
        state = s.expect("folder loaded in the file list", {
            "paneCount": 1, "panes.0.viewId": "dolphin", "panes.0.loading": False,
            "panes.0.items": ["sub", "a.txt", "b.txt", "c.txt"], "focus.role": "dolphin-item-view"})
        if "whale_testlist" not in state["availableViews"]:
            raise Failure(f"no test list view: {state['availableViews']}")

        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("test list shown", {
            "panes.0.viewId": "whale_testlist", "panes.0.dolphinViewVisible": False,
            "panes.0.pluginWidgetVisible": True, "panes.0.focusProxy": "plugin-widget",
            "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"], "focus.role": "plugin:whale_testlist"})

        client.tool("set_location", pane=0, url=sub_url)
        s.expect("navigated into a folder", {
            "url": sub_url, "panes.0.loading": False, "panes.0.pluginRows": ["deeper", "inner.txt"],
            "panes.0.viewId": "whale_testlist", "focus.role": "plugin:whale_testlist"})
        client.tool("trigger_action", action="go_back")
        s.expect("back in the history", {
            "panes.0.loading": False, "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"],
            "focus.role": "plugin:whale_testlist"})

        client.tool("set_sort", pane=0, field="size", order="ascending", folders_first=False)
        state = s.expect("sorted by size", {"panes.0.sort": {"field": "size", "order": "ascending", "foldersFirst": False}})
        files = [row for row in state["panes"][0]["pluginRows"] if row != "sub"]
        if files != ["b.txt", "c.txt", "a.txt"]:
            raise Failure(f"size order in the test list: {files}")
        client.tool("set_sort", pane=0, field="name", order="descending", folders_first=True)
        s.expect("sorted by name, descending", {"panes.0.pluginRows": ["sub", "c.txt", "b.txt", "a.txt"]})
        client.tool("set_sort", pane=0, order="ascending")
        s.expect("sorted by name, ascending", {"panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"]})

        client.tool("set_hidden_files", pane=0, shown=True)
        s.expect("hidden files shown", {"panes.0.hiddenFiles": True, "panes.0.pluginRows": ["sub", ".hidden", "a.txt", "b.txt", "c.txt"]})
        client.tool("set_hidden_files", pane=0, shown=False)
        s.expect("hidden files hidden", {"panes.0.hiddenFiles": False, "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"]})

        client.tool("set_filter_text", pane=0, text="b.")
        # FB2: the filter applies to files by default, the folder stays shown.
        s.expect("name filter set", {"panes.0.filter": {"text": "b.", "barVisible": True, "scope": "files"},
                                     "panes.0.pluginRows": ["sub", "b.txt"],
                                     "focus.role": "filter-bar"})
        client.tool("clear_filter", pane=0)
        s.expect("name filter cleared", {"panes.0.filter": {"text": "", "barVisible": False, "scope": "files"},
                                         "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"],
                                         "focus.role": "plugin:whale_testlist"})

        client.tool("set_split_view", enabled=True)
        s.expect("split view: test list left, Dolphin right", {"splitAnimating": False,
            "paneCount": 2, "activePane": 1, "panes.0.viewId": "whale_testlist", "panes.1.viewId": "dolphin",
            "panes.1.loading": False, "focus.role": "dolphin-item-view", "focus.pane": 1})

        client.tool("activate_pane", pane="left")
        s.expect("left pane activated", {"activePane": 0, "panes.0.active": True, "panes.1.active": False,
                                         "focus.role": "plugin:whale_testlist", "focus.pane": 0})
        client.tool("trigger_action", action="focus_other_view")
        s.expect("focus other view: Dolphin", {"activePane": 1, "focus.role": "dolphin-item-view", "focus.pane": 1})
        client.tool("trigger_action", action="focus_other_view")
        s.expect("focus other view: test list", {"activePane": 0, "focus.role": "plugin:whale_testlist", "focus.pane": 0})

        before = client.state()["activations"]["count"]
        client.tool("set_view", pane=1, view="whale_testlist")
        state = s.expect("test list on both sides, the inactive side stays inactive", {
            "panes.1.viewId": "whale_testlist", "activePane": 0, "focus.pane": 0})
        if state["activations"]["count"] != before:
            raise Failure(f"switching the inactive pane activated it: {state['activations']}")
        client.tool("set_view", pane=1, view="dolphin")
        s.expect("right side back to Dolphin", {"panes.1.viewId": "dolphin", "panes.1.dolphinViewVisible": True,
                                                "panes.1.focusProxy": "dolphin-item-view"})

        client.tool("set_view", pane=0, view="dolphin")
        s.expect("left side: test list -> Dolphin", {"panes.0.viewId": "dolphin", "panes.0.dolphinViewVisible": True,
                                                    "focus.role": "dolphin-item-view", "focus.pane": 0})
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("left side: Dolphin -> test list", {"panes.0.viewId": "whale_testlist", "focus.role": "plugin:whale_testlist",
                                                    "focus.pane": 0})

        client.tool("set_split_view", enabled=False)
        s.expect("split view closed", {"paneCount": 1})
        image = client.tool("screenshot")[0]
        if image["type"] != "image" or len(image["data"]) < 1000:
            raise Failure("no screenshot")
        print("ok    screenshot")
    finally:
        client.close()
        for process in (whale, plain):
            if process is None:
                continue
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--whale", required=True)
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--plugin-dir", required=True)
    parser.add_argument("--platform", default="offscreen", help="empty for the session's platform")
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        print("skipped: no D-Bus session bus")
        return 77
    if args.screenshots:
        os.makedirs(args.screenshots, exist_ok=True)

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-m3-"))
    try:
        run(args, work)
    except Failure as failure:
        print(f"FAILED: {failure}")
        print(f"(Whale log: {work / 'profile' / 'whale.log'})")
        args.keep = True
        return 1
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
    print("M3 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
