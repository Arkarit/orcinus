#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of F1 ("Show flat (no folders)": the file list shows the files below its
location), driven only through the dev MCP; a restart of Whale is done here.

  A Details with subfolders, deep files shown     B Icons: the scope stays
  C Compact: the scope stays                       D filter on the files below
  E sorting by size and name                       F several deep files, Duplicate and Trash
  G split view: recursive as source, never as target (drop refused and explained)
  H history                                        K Paste and drops refused, explained,
  J preferred view (O3) and scope together           no operation; normal again without scope
  I session restart                                L regressions: see the M3–O3 acceptances

Files are changed only in a temporary folder; Whale runs with a test profile of its own
(its home, trash and clipboard; file operations allowed) on a private D-Bus session bus.
On a real desktop the clipboard is the user's: Paste is checked offscreen only.
Arguments as test_m6_acceptance.py.
"""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance

TREEMAP = "orcinus_treemap"
RECURSIVE = "recursive-files"
CURRENT = "current-folder"


class Session:
    """One Whale process with its MCP client, in the profile that survives restarts."""

    def __init__(self, args, work):
        self.whale = start_whale(args.whale, None, work / "profile", args.plugin_dir, args.platform, automation=True,
                                 opener_log=work / "opened.log", file_operations=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_f1_acceptance", "version": "1"}})
        self.client.notify("notifications/initialized")
        wait_for_instance(self.client, self.whale)

    def quit(self):
        self.client.tool("quit")
        try:
            self.whale.wait(timeout=20)
        except subprocess.TimeoutExpired:
            raise Failure("Whale did not quit")
        self.client.close()

    def kill(self):
        self.client.close()
        if self.whale.poll() is None:
            self.whale.terminate()
            self.whale.wait(timeout=10)


def state(client):
    return json.loads(client.tool("get_ui_state")[0]["text"])


def run(args, work):
    root = work / "files" / "root"
    files = {
        "top.txt": 300,
        "a/one.txt": 100,
        "a/b/two.png": 5000,
        "a/b/c/three.txt": 200,
        ".hidden.txt": 10,
        ".hiddendir/secret.txt": 10,
    }
    for name, size in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(b"x" * size)
    (root / "empty").mkdir()
    target = work / "files" / "target"
    target.mkdir()
    (target / "existing.txt").write_bytes(b"e" * 10)
    uri = lambda path: path.as_uri()
    r = uri(root)
    by_name = ["a/one.txt", "a/b/c/three.txt", "top.txt", "a/b/two.png"]
    offscreen = args.platform == "offscreen"

    w = Session(args, work)
    try:
        s = Scenario(w.client, args.screenshots)
        client = w.client

        client.tool("set_location", pane=0, url=r)
        client.tool("set_hidden_files", pane=0, shown=False)
        client.tool("set_sort", pane=0, field="name", order="ascending", folders_first=True)
        s.expect("start: the location's own items", {
            "url": r, "panes.0.loading": False, "panes.0.viewId": "dolphin", "panes.0.itemCount": 3,
            "panes.0.contentScope.scope": CURRENT, "panes.0.contentScope.shown": False,
            "panes.0.contentScope.supported": True})

        # A: Details with subfolders
        client.tool("trigger_action", action="view_details")
        client.tool("set_content_scope", pane=0, scope=RECURSIVE)
        s.expect("A: Details, Show flat (no folders): the files below, no folders", {
            "url": r, "panes.0.loading": False, "panes.0.viewMode": "details", "panes.0.itemCount": 4,
            "panes.0.contentScope.shown": True, "panes.0.contentScope.scope": RECURSIVE,
            "panes.0.contentScope.origin": "user", "panes.0.contentScope.preferred": RECURSIVE,
            "panes.0.contentScope.relativeItems": by_name, "panes.0.viewId": "dolphin"})

        # B, C: the view modes keep the scope
        client.tool("trigger_action", action="view_icons")
        s.expect("B: Icons: the scope stays", {
            "panes.0.viewMode": "icons", "panes.0.contentScope.shown": True, "panes.0.contentScope.relativeItems": by_name})
        client.tool("trigger_action", action="view_compact")
        s.expect("C: Compact: the scope stays", {
            "panes.0.viewMode": "compact", "panes.0.contentScope.shown": True, "panes.0.contentScope.relativeItems": by_name})

        # D: filter
        client.tool("set_filter_text", pane=0, text="*.png")
        s.expect("D: filter *.png: only the matching deep file", {
            "panes.0.filter.text": "*.png", "panes.0.contentScope.relativeItems": ["a/b/two.png"]})
        client.tool("clear_filter", pane=0)
        s.expect("D: filter cleared", {"panes.0.filter.text": "", "panes.0.contentScope.relativeItems": by_name})

        # E: sorting
        client.tool("set_sort", pane=0, field="size", order="ascending")
        s.expect("E: by size", {"panes.0.sort.field": "size",
                                "panes.0.contentScope.relativeItems": ["a/one.txt", "a/b/c/three.txt", "top.txt", "a/b/two.png"]})
        client.tool("set_sort", pane=0, field="name", order="descending")
        s.expect("E: by name, descending", {"panes.0.contentScope.relativeItems": list(reversed(by_name))})
        client.tool("set_sort", pane=0, field="name", order="ascending")
        s.expect("E: by name", {"panes.0.contentScope.relativeItems": by_name})

        # F: several deep files, actions on their real URLs
        three, one = uri(root / "a/b/c/three.txt"), uri(root / "a/one.txt")
        client.tool("set_selection", pane=0, urls=[three, one])
        s.expect("F: two deep files selected", {"panes.0.selectionCount": 2, "panes.0.selection": {"$contains": three}})
        # Nothing is created among items from many folders; removing and moving away work.
        s.expect("F: no duplicate, no paste, no new items; trash and cut yes", {
            "actions.duplicate.enabled": False, "actions.duplicate.gate": "no destination folder",
            "actions.edit_paste.gate": "no destination folder", "actions.create_dir.gate": "no destination folder",
            "actions.new_menu.gate": "no destination folder",
            "actions.movetotrash.enabled": True, "actions.edit_cut.enabled": True})
        if "disabled" not in (client.tool_error("trigger_action", action="duplicate") or ""):
            raise Failure("F: duplicate was not refused")
        time.sleep(0.3)
        if (root / "a/one copy.txt").exists() or (root / "a/b/c/three copy.txt").exists():
            raise Failure("F: duplicated anyway")

        # G: split view
        client.tool("set_split_view", enabled=True)
        s.expect("G: the new right half at the same location: its preference applies", {
            "paneCount": 2, "activePane": 1, "splitAnimating": False, "panes.1.url": r, "panes.1.contentScope.shown": True,
            "panes.1.contentScope.origin": "preference"})
        client.tool("set_location", pane=1, url=uri(target))
        s.expect("G: right: the target folder, current folder", {
            "panes.1.url": uri(target), "panes.1.loading": False, "panes.1.contentScope.shown": False,
            "panes.0.contentScope.shown": True})
        client.tool("activate_pane", pane=0)
        client.tool("set_selection", pane=0, urls=[uri(root / "a/b/two.png")])
        drag = json.loads(client.tool("perform_drag_drop", source_pane=0, target_pane=1, action="copy")[0]["text"])
        if not drag.get("dropped"):
            raise Failure(f"G: the drag out of the recursive view was not dropped: {drag}")
        s.expect("G: a deep file dragged out to the right half", {
            "panes.1.items": {"$contains": "two.png"}, "panes.0.contentScope.refusals": 0})
        if not (target / "two.png").exists() or not (root / "a/b/two.png").exists():
            raise Failure("G: the drag out did not copy the deep file")
        client.tool("activate_pane", pane=1)
        client.tool("set_selection", pane=1, urls=[uri(target / "existing.txt")])
        drag = json.loads(client.tool("perform_drag_drop", source_pane=1, target_pane=0, action="copy")[0]["text"])
        if drag.get("accepted") or drag.get("dropped"):
            raise Failure(f"G: a drop into the recursive view was accepted: {drag}")
        state_g = s.expect("G: dropping into the recursive view: refused and explained", {
            "panes.0.contentScope.refusals": 1, "panes.0.contentScope.lastRefusal": {"$contains": ""}})
        if state_g["panes"][0]["message"] != state_g["panes"][0]["contentScope"]["lastRefusal"]:
            raise Failure("G: the explanation is not shown in the view area")
        drag = json.loads(client.tool("perform_drag_drop", source_pane=1, target_pane=0, target_url=three, action="copy")[0]["text"])
        if drag.get("accepted") or drag.get("dropped"):
            raise Failure(f"G: a drop onto a file of the recursive view was accepted: {drag}")
        s.expect("G: onto a file: refused as well", {"panes.0.contentScope.refusals": 2})
        if any(path.name == "existing.txt" for path in root.rglob("*")):
            raise Failure("G: a refused drop changed files below the recursive view")
        client.tool("set_split_view", enabled=False)
        s.expect("G: split closed", {"paneCount": 1})

        # H: history
        client.tool("set_location", pane=0, url=r)
        s.expect("H: at the location with subfolders", {"url": r, "panes.0.contentScope.shown": True, "panes.0.loading": False})
        client.tool("set_location", pane=0, url=uri(target))
        s.expect("H: elsewhere: the default", {
            "url": uri(target), "panes.0.contentScope.shown": False, "panes.0.contentScope.origin": "default"})
        client.tool("trigger_action", action="go_back")
        s.expect("H: back: with subfolders, from the history", {
            "url": r, "panes.0.loading": False, "panes.0.contentScope.shown": True,
            "panes.0.contentScope.origin": "history", "panes.0.itemCount": 4})
        client.tool("trigger_action", action="go_forward")
        s.expect("H: forward: the current folder", {"url": uri(target), "panes.0.contentScope.shown": False,
                                                     "panes.0.contentScope.origin": "history"})
        client.tool("trigger_action", action="go_back")
        s.expect("H: back again", {"url": r, "panes.0.contentScope.shown": True, "panes.0.loading": False})

        # K: Paste and drops are refused; without the scope they work again
        if offscreen:
            client.tool("set_content_scope", pane=0, scope=CURRENT)
            s.expect("K: current folder", {"panes.0.contentScope.shown": False, "panes.0.loading": False})
            client.tool("set_location", pane=0, url=uri(target))
            s.expect("K: target", {"url": uri(target), "panes.0.loading": False})
            client.tool("set_selection", pane=0, urls=[uri(target / "existing.txt")])
            client.tool("trigger_action", action="copy")
            client.tool("set_location", pane=0, url=r)
            client.tool("set_content_scope", pane=0, scope=RECURSIVE)
            s.expect("K: with subfolders, something to paste: Paste is off (no destination folder)", {
                "url": r, "panes.0.contentScope.shown": True, "panes.0.loading": False,
                "panes.0.contentScope.pasteRefusal": {"$contains": ""},
                "actions.edit_paste.enabled": False, "actions.edit_paste.gate": "no destination folder"})
            if "disabled" not in (client.tool_error("trigger_action", action="paste") or ""):
                raise Failure("K: Paste was not refused")
            time.sleep(0.3)
            if (root / "existing.txt").exists():
                raise Failure("K: the refused Paste copied a file")
            client.tool("set_content_scope", pane=0, scope=CURRENT)
            s.expect("K: current folder again: Paste is Dolphin's", {
                "panes.0.contentScope.shown": False, "panes.0.contentScope.pasteRefusal": None, "panes.0.loading": False})
            client.tool("trigger_action", action="paste")
            # Offscreen nothing activates the window again after KIO's copy job (see Scenario).
            s.require_keyboard_focus = False
            s.expect("K: pasted into the location", {"panes.0.items": {"$contains": "existing.txt"}})
            client.tool("set_content_scope", pane=0, scope=RECURSIVE)
            s.expect("K: with subfolders again", {"panes.0.contentScope.shown": True, "panes.0.itemCount": 5})
        else:
            print("   K: Paste skipped outside offscreen (the clipboard is the user's)")

        # J: O3 and F1 together: a prefers the treemap, root the file list with subfolders
        a = uri(root / "a")
        client.tool("set_location", pane=0, url=a)
        s.expect("J: a: no scope of its own (no inheritance)", {
            "url": a, "panes.0.contentScope.shown": False, "panes.0.contentScope.origin": "default"})
        client.tool("set_view", pane=0, view=TREEMAP, remember=True)
        s.expect("J: a: treemap chosen", {"panes.0.viewId": TREEMAP, "panes.0.preferredView": TREEMAP,
                                          "panes.0.contentScope.supported": False, "panes.0.treemap.status": "ready"})
        client.tool("set_location", pane=0, url=r)
        s.expect("J: root: the treemap stays (no preferred view), the scope waits for the file list", {
            "url": r, "panes.0.viewId": TREEMAP, "panes.0.contentScope.scope": RECURSIVE,
            "panes.0.contentScope.shown": False})
        client.tool("set_view", pane=0, view="dolphin", remember=True)
        s.expect("J: root: file list chosen: with subfolders", {
            "panes.0.viewId": "dolphin", "panes.0.preferredView": "dolphin", "panes.0.contentScope.shown": True,
            "panes.0.loading": False})
        for i in range(2):
            client.tool("set_location", pane=0, url=a)
            s.expect(f"J{i}: a: treemap by preference", {"url": a, "panes.0.viewId": TREEMAP,
                                                         "panes.0.viewOrigin": "preference", "panes.0.treemap.status": "ready"})
            client.tool("set_location", pane=0, url=r)
            s.expect(f"J{i}: root: file list with subfolders by preference", {
                "url": r, "panes.0.viewId": "dolphin", "panes.0.viewOrigin": "preference",
                "panes.0.contentScope.shown": True, "panes.0.contentScope.origin": "preference", "panes.0.loading": False})

        # I: session restart
        w.quit()
        w = Session(args, work)
        client = w.client
        s.client = client
        s.expect("I: restored with subfolders", {
            "url": r, "panes.0.viewId": "dolphin", "panes.0.contentScope.shown": True,
            "panes.0.contentScope.origin": "session", "panes.0.loading": False}, timeout_ms=20000)
        w.quit()
    except BaseException:
        w.kill()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--whale", required=True, help="the whale binary of the build tree")
    parser.add_argument("--adapter", required=True, help="whale_dev_mcp.py")
    parser.add_argument("--plugin-dir", required=True, help="the view plugin directories (separated by ':')")
    parser.add_argument("--platform", default="offscreen", help='QT_QPA_PLATFORM for Whale ("" = the session\'s)')
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_F1_PRIVATE_BUS") != "1":
        # Run again on a private session bus of our own (see test_m6_acceptance.py).
        env = dict(os.environ, WHALE_F1_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
        with tempfile.TemporaryFile() as output:
            code = subprocess.call(["dbus-run-session", "--", sys.executable, *sys.argv], env=env, stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT)
            output.seek(0)
            for line in output.read().decode(errors="replace").splitlines():
                if not line.startswith(("dbus-daemon[", "kf.wallet", "qt.qpa.services", "A connection to the bus")):
                    print(line)
        return code
    if args.screenshots:
        os.makedirs(args.screenshots, exist_ok=True)

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-f1-"))
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
    print("F1 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
