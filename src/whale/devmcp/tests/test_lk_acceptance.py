#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the linked split view (LK, .privatedoc/whale-split-lock-plan.md),
driven only through the dev MCP.

  A  the button in the right view      B  locking takes the active location
  C  the other view follows            D  the selection is shown in the other view
  E  the treemap takes part            F  a view without LinkedSelection releases the lock
  G  an unlockable location releases   H  closing the split view releases

Arguments as test_m4_acceptance.py.
"""

import argparse
import os
import pathlib
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance

TREEMAP = "whale_treemap"
TESTLIST = "whale_testlist"


def run(args, work):
    folder = work / "files"
    (folder / "sub" / "deeper").mkdir(parents=True)
    (folder / "other").mkdir()
    for name, size in (("a.txt", 3000), ("b.txt", 2000)):
        (folder / name).write_bytes(b"x" * size)
    (folder / "sub" / "inner.txt").write_bytes(b"i" * 4000)
    (folder / "sub" / "deeper" / "deep.txt").write_bytes(b"d" * 5000)
    (folder / "other" / "x.txt").write_bytes(b"x" * 100)

    def url(*parts):
        return folder.joinpath(*parts).as_uri()

    whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_lk_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)

        client.tool("set_hidden_files", shown=False)
        client.tool("set_sort", field="name", order="ascending", folders_first=True)
        s.expect("folder loaded", {"panes.0.loading": False, "panes.0.items": ["other", "sub", "a.txt", "b.txt"]})
        if client.state()["splitLock"]["buttonVisible"]:
            raise Failure("the lock button is shown without a split view")
        if client.tool_error("set_split_lock", locked=True) is None:
            raise Failure("locking without a split view did not fail")

        # A: the button in the right view, unlocked
        client.tool("set_split_view", enabled=True)
        s.expect("A: split view with the lock button", {
            "splitAnimating": False, "paneCount": 2, "panes.1.loading": False,
            "splitLock.locked": False, "splitLock.buttonVisible": True, "splitLock.buttonEnabled": True,
            "splitLock.buttonIcon": "remove-link", "splitLock.toolTip": {"$contains": "Link Views"},
            "splitLock.panes.0.lockable": True, "splitLock.panes.1.lockable": True,
            "splitLock.panes.0.selectionLimited": False})

        # B: locking takes the location of the active view
        client.tool("set_location", pane=1, url=url("other"))
        s.expect("B: right view elsewhere", {"panes.1.url": url("other"), "panes.1.loading": False})
        client.tool("activate_pane", pane="left")
        s.expect("B: left view active", {"activePane": 0})
        reply = client.tool("set_split_lock", locked=True)
        if '"locked":true' not in reply[0]["text"].replace(" ", ""):
            raise Failure(f"set_split_lock: {reply}")
        s.expect("B: locked, the right view took the left location", {
            "splitLock.locked": True, "splitLock.buttonChecked": True, "splitLock.buttonIcon": "link",
            "splitLock.toolTip": "Unlink Views", "splitLock.panes.0.selectionLimited": True,
            "splitLock.panes.1.selectionLimited": True, "panes.1.url": folder.as_uri(), "activePane": 0})

        # C: each view follows the other one, the following view stays inactive
        client.tool("set_location", pane=0, url=url("sub"))
        s.expect("C: right follows left", {"panes.0.url": url("sub"), "panes.1.url": url("sub"),
                                           "panes.1.loading": False, "activePane": 0})
        client.tool("trigger_action", action="go_up")
        up = folder.as_uri() + "/"  # Up ends the URL with a slash
        s.expect("C: right follows Up", {"panes.0.url": up, "panes.1.url": up, "panes.1.loading": False, "activePane": 0})

        # D: the selection of the active view in the other one
        client.tool("set_selection", pane=0, urls=[url("a.txt"), url("b.txt")], current=url("b.txt"))
        s.expect("D: selection shown on the right", {
            "panes.1.selectedNames": ["a.txt", "b.txt"], "panes.1.currentName": "b.txt",
            "panes.0.selectedNames": ["a.txt", "b.txt"], "activePane": 0})

        # E: the treemap on the right takes part, in both directions
        client.tool("set_view", pane=1, view=TREEMAP)
        s.expect("E: treemap shown, still locked", {
            "panes.1.viewId": TREEMAP, "panes.1.treemap.status": "ready", "splitLock.locked": True,
            "splitLock.panes.1.lockable": True}, focus=False)
        client.tool("activate_pane", pane="left")
        client.tool("set_selection", pane=0, urls=[url("sub")], current=url("sub"))
        s.expect("E: the treemap shows the left selection", {
            "activePane": 0, "panes.1.treemap.selected": url("sub")})
        client.tool("activate_pane", pane="right")
        client.tool("treemap_select", pane=1, url=url("b.txt"))
        s.expect("E: the left view shows the treemap's selection", {
            "activePane": 1, "panes.0.selectedNames": ["b.txt"], "panes.0.currentName": "b.txt"})
        client.tool("treemap_select", pane=1, url=url("sub", "deeper", "deep.txt"))
        # The left view shows icons, which cannot expand: the top-level folder becomes current.
        s.expect("E: a deep treemap entry: its top-level folder is current on the left", {
            "panes.0.selectedNames": [], "panes.0.currentName": "sub",
            "panes.1.treemap.selected": url("sub", "deeper", "deep.txt")})

        # F: a view without LinkedSelection releases the lock, with a message
        client.tool("set_view", pane=1, view=TESTLIST)
        s.expect("F: released by the test list", {
            "panes.1.viewId": TESTLIST, "splitLock.locked": False, "splitLock.buttonEnabled": False,
            "splitLock.panes.1.lockable": False, "splitLock.panes.1.reason": "This view cannot be linked.",
            "splitLock.toolTip": "The views cannot be linked. This view cannot be linked.",
            "panes.1.message": "The views are no longer linked. This view cannot be linked.",
            "splitLock.panes.0.selectionLimited": False})
        error = client.tool_error("set_split_lock", locked=True)
        if not error or "disabled" not in error:
            raise Failure(f"locking with the test list: {error}")
        client.tool("set_view", pane=1, view="dolphin")
        s.expect("F: lockable again, not locked again by itself", {
            "panes.1.viewId": "dolphin", "splitLock.buttonEnabled": True, "splitLock.locked": False})

        # G: an unlockable location releases the lock; the other view stays
        client.tool("activate_pane", pane="left")
        client.tool("set_split_lock", locked=True)
        s.expect("G: locked again", {"splitLock.locked": True, "activePane": 0})
        client.tool("set_location", pane=0, url="trash:/")
        s.expect("G: released in the trash", {
            "panes.0.url": "trash:/", "panes.1.url": up, "splitLock.locked": False,
            "panes.0.message": "The views are no longer linked. Only local folders can be linked.",
            "splitLock.panes.0.reason": "Only local folders can be linked."})
        client.tool("set_location", pane=0, url=folder.as_uri())
        s.expect("G: back, not locked again by itself", {"panes.0.url": folder.as_uri(), "splitLock.buttonEnabled": True,
                                                         "splitLock.locked": False})

        # H: closing the split view releases the lock
        client.tool("set_split_lock", locked=True)
        s.expect("H: locked", {"splitLock.locked": True})
        client.tool("set_split_view", enabled=False)
        s.expect("H: released with the split view", {"paneCount": 1, "splitLock.locked": False,
                                                     "splitLock.buttonVisible": False})
        client.tool("set_split_view", enabled=True)
        s.expect("H: a new split view is not locked", {"splitAnimating": False, "paneCount": 2, "splitLock.locked": False,
                                                       "splitLock.buttonVisible": True})
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-lk-"))
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
    print("LK acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
