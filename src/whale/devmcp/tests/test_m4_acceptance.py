#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the view plugin milestone M4 (selection and activation),
driven only through the dev MCP.

  A  Dolphin -> test list     B  test list -> Dolphin     C  multiple items
  D  current item             E  split view               F  activation

Text files open with a test "opener" installed in Whale's own profile, so no real
application is started. Arguments as test_m3_acceptance.py.
"""

import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile
import time

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance


def wait_for_file_lines(path, count, timeout=15):
    """The opener is a separate process; only its log tells that it ran."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            lines = [line for line in path.read_text().splitlines() if line]
            if len(lines) >= count:
                return lines
        time.sleep(0.1)
    raise Failure(f"the test opener did not run ({path})")


def run(args, work):
    folder = work / "files"
    (folder / "sub").mkdir(parents=True)
    (folder / "other").mkdir()
    for name, size in (("a.txt", 300), ("b.txt", 100), ("c.txt", 200)):
        (folder / name).write_bytes(b"x" * size)
    (folder / "sub" / "inner.txt").write_bytes(b"i")
    (folder / "other" / "x.txt").write_bytes(b"x")
    (folder / "other" / "y.txt").write_bytes(b"y")

    def url(*parts):
        return folder.joinpath(*parts).as_uri()

    opener_log = work / "opened.log"
    whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True,
                        opener_log=opener_log)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_m4_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)

        client.tool("set_hidden_files", shown=False)
        client.tool("set_sort", field="name", order="ascending", folders_first=True)
        s.expect("folder loaded", {"panes.0.loading": False, "panes.0.items": ["other", "sub", "a.txt", "b.txt", "c.txt"],
                                   "focus.role": "dolphin-item-view"})

        # A: Dolphin -> test list
        client.tool("set_selection", pane=0, urls=[url("a.txt"), url("c.txt")], current=url("c.txt"))
        s.expect("A: selected in Dolphin", {
            "panes.0.viewId": "dolphin", "panes.0.selectedNames": ["a.txt", "c.txt"], "panes.0.currentName": "c.txt",
            "shell.selectedNames": ["a.txt", "c.txt"]})
        # The status bar follows the DolphinView's (debounced) selection signal.
        s.expect("A: status bar shows the selection", {"panes.0.statusText": {"$contains": "2 files selected"}})
        s.expect("A: status bar shows the size", {"panes.0.statusText": {"$contains": "500 B"}})
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("A: same selection in the test list", {
            "panes.0.viewId": "whale_testlist", "panes.0.pluginSelection": ["a.txt", "c.txt"],
            "panes.0.pluginCurrent": "c.txt", "panes.0.selectedNames": ["a.txt", "c.txt"],
            "focus.role": "plugin:whale_testlist"})

        # B: test list -> Dolphin
        client.tool("set_selection", pane=0, urls=[url("b.txt")], current=url("b.txt"))
        s.expect("B: changed while the test list is shown", {
            "panes.0.pluginSelection": ["b.txt"], "panes.0.pluginCurrent": "b.txt", "panes.0.selectedNames": ["b.txt"],
            "shell.selectedNames": ["b.txt"]})
        client.tool("set_view", pane=0, view="dolphin")
        s.expect("B: same selection in Dolphin", {
            "panes.0.viewId": "dolphin", "panes.0.selectedNames": ["b.txt"], "panes.0.currentName": "b.txt",
            "shell.selectedNames": ["b.txt"]})

        # C: several items survive switching back and forth
        all_items = ["other", "sub", "a.txt", "b.txt", "c.txt"]
        client.tool("trigger_action", action="select_all")
        s.expect("C: everything selected in Dolphin", {"panes.0.selectedNames": all_items})
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("C: everything selected in the test list", {"panes.0.pluginSelection": all_items,
                                                             "panes.0.selectedNames": all_items})
        client.tool("trigger_action", action="invert_selection")
        s.expect("C: inverted while the test list is shown", {"panes.0.pluginSelection": [],
                                                             "panes.0.selectedNames": []})
        client.tool("set_selection", pane=0, urls=[url("sub"), url("a.txt"), url("c.txt")])
        client.tool("set_view", pane=0, view="dolphin")
        s.expect("C: three items back in Dolphin", {"panes.0.selectedNames": ["sub", "a.txt", "c.txt"]})

        # D: the current item is independent of the selection and survives switching
        client.tool("set_selection", pane=0, urls=[url("a.txt")], current=url("c.txt"))
        for view, title in (("whale_testlist", "test list"), ("dolphin", "Dolphin"), ("whale_testlist", "test list")):
            client.tool("set_view", pane=0, view=view)
            conditions = {"panes.0.viewId": view, "panes.0.selectedNames": ["a.txt"], "panes.0.currentName": "c.txt"}
            if view != "dolphin":
                conditions.update({"panes.0.pluginSelection": ["a.txt"], "panes.0.pluginCurrent": "c.txt"})
            s.expect(f"D: current item kept ({title})", conditions)

        # Outside of the listing: ignored and reported.
        reply = json.loads(client.tool("set_selection", pane=0, urls=[url("sub", "inner.txt"), url("b.txt")])[0]["text"])
        if reply["ignored"] != [url("sub", "inner.txt")]:
            raise Failure(f"ignored URLs: {reply}")
        s.expect("deep URL ignored", {"panes.0.selectedNames": ["b.txt"], "panes.0.pluginSelection": ["b.txt"]})

        # E: split view, two separate selections
        client.tool("set_split_view", enabled=True)
        s.expect("E: split view", {"paneCount": 2, "activePane": 1, "panes.1.loading": False})
        client.tool("set_location", pane=1, url=url("other"))
        s.expect("E: right side in another folder", {"panes.1.url": url("other"), "panes.1.loading": False})
        client.tool("set_selection", pane=1, urls=[url("other", "y.txt")], current=url("other", "y.txt"))
        client.tool("set_selection", pane=0, urls=[url("a.txt"), url("b.txt")], current=url("b.txt"))
        separate = {"panes.0.selectedNames": ["a.txt", "b.txt"], "panes.0.currentName": "b.txt",
                    "panes.0.pluginSelection": ["a.txt", "b.txt"],
                    "panes.1.selectedNames": ["y.txt"], "panes.1.currentName": "y.txt"}
        s.expect("E: separate selections", dict(separate, **{"shell.selectedNames": ["y.txt"]}))
        client.tool("activate_pane", pane="left")
        s.expect("E: left active, selections unchanged", dict(separate, **{
            "activePane": 0, "focus.role": "plugin:whale_testlist", "shell.selectedNames": ["a.txt", "b.txt"]}))
        client.tool("trigger_action", action="focus_other_view")
        s.expect("E: focus other view -> right, selections unchanged", dict(separate, **{
            "activePane": 1, "focus.role": "dolphin-item-view", "shell.selectedNames": ["y.txt"]}))
        client.tool("trigger_action", action="focus_other_view")
        s.expect("E: focus other view -> left, selections unchanged", dict(separate, **{"activePane": 0}))
        # Closing the split view closes the active (left) side, like the Close button;
        # the right side stays with its own selection.
        client.tool("set_split_view", enabled=False)
        s.expect("E: split view closed, the other side kept its selection", {
            "paneCount": 1, "url": url("other"), "panes.0.viewId": "dolphin", "panes.0.selectedNames": ["y.txt"]})
        client.tool("set_location", pane=0, url=folder.as_uri())
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("F: test list on the folder", {"url": folder.as_uri(), "panes.0.loading": False,
                                                "panes.0.viewId": "whale_testlist", "focus.role": "plugin:whale_testlist"})

        # F: activation through the host
        client.tool("activate_items", pane=0, urls=[url("sub")])
        s.expect("F: folder entered, test list stays", {
            "url": url("sub"), "panes.0.loading": False, "panes.0.viewId": "whale_testlist",
            "panes.0.pluginRows": ["inner.txt"], "panes.0.selectedNames": [], "focus.role": "plugin:whale_testlist"})
        client.tool("trigger_action", action="go_back")
        s.expect("F: back", {"panes.0.loading": False, "panes.0.pluginRows": ["other", "sub", "a.txt", "b.txt", "c.txt"]})
        client.tool("activate_items", pane=0, urls=[url("a.txt")])
        opened = wait_for_file_lines(opener_log, 1)
        if not opened[0].endswith("/a.txt"):
            raise Failure(f"opened: {opened}")
        s.expect("F: file opened through the usual path, location unchanged", {"panes.0.pluginRows": ["other", "sub", "a.txt", "b.txt", "c.txt"]})
        print("ok    F: test opener got", opened[0])
        client.tool("activate_items", pane=0, urls=[url("b.txt"), url("c.txt")])
        opened = wait_for_file_lines(opener_log, 3)
        if sorted(pathlib.Path(line).name for line in opened[1:]) != ["b.txt", "c.txt"]:
            raise Failure(f"opened: {opened}")
        print("ok    F: two files opened at once")
    finally:
        client.close()
        whale.terminate()
        try:
            whale.wait(timeout=10)
        except Exception:
            whale.kill()


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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-m4-"))
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
    print("M4 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
