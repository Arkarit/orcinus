#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the view plugin milestone M6 (persistence), driven only through
the dev MCP; restarts of Whale are done here, as a user would start it again.

  A view state across view switches   B history back/forward   C split view
  D session restart                   E old session without Whale data
  F missing plugin                    G capabilities after a restore

Whale runs on a private D-Bus session bus (the script starts one): Dolphin restores
its session only when it is the only instance, and the user's own Whale must not count.
Arguments as test_m3_acceptance.py, plus --dynamic-plugin-dir (the test plugin
whale_test_dynamic).
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance


class Session:
    """One Whale process with its MCP client, in the profile that survives restarts."""

    def __init__(self, args, work, plugins=True):
        self.whale = start_whale(args.whale, None, work / "profile", args.plugin_dir if plugins else None, args.platform,
                                 automation=True, extra_plugin_dirs=(args.dynamic_plugin_dir,) if plugins else ())
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_m6_acceptance", "version": "1"}})
        self.client.notify("notifications/initialized")
        wait_for_instance(self.client, self.whale)

    def quit(self):
        """Closes the window like the user; Whale saves its session and exits."""
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


def session_files(profile):
    return [path for path in profile.rglob("*") if path.is_file() and b"WhaleViews" in path.read_bytes()]


def run(args, work):
    root = work / "files"
    (root / "b").mkdir(parents=True)
    (root / "z-zoom").mkdir()
    for i in range(120):
        (root / f"f{i:03}.txt").write_bytes(b"f")
    for i in range(60):
        (root / "b" / f"g{i:03}.txt").write_bytes(b"g")
    (root / "z-zoom" / "z.txt").write_bytes(b"z")
    url = root.as_uri()
    url_b = (root / "b").as_uri()
    url_zoom = (root / "z-zoom").as_uri()
    root_items = ["b", "z-zoom"] + [f"f{i:03}.txt" for i in range(120)]

    w = Session(args, work)
    try:
        s = Scenario(w.client, args.screenshots)
        client = w.client
        client.tool("set_hidden_files", shown=False)
        client.tool("set_sort", field="name", order="ascending", folders_first=True)
        client.tool("set_location", pane=0, url=url)
        s.expect("folder loaded", {"url": url, "panes.0.loading": False, "panes.0.itemCount": 122})

        # A: the view state survives switching views
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("A: test list", {"panes.0.viewId": "whale_testlist", "panes.0.pluginRows.121": "f119.txt"})
        client.tool("scroll_to_item", pane=0, url=(root / "f060.txt").as_uri())
        s.expect("A: scrolled to f060", {"panes.0.pluginTopItem": "f060.txt"})
        client.tool("set_view", pane=0, view="dolphin")
        s.expect("A: file list; the test list's state is kept", {"panes.0.viewId": "dolphin",
                                                                 "panes.0.storedViewStates": ["whale_testlist"]})
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("A: test list again, at f060", {"panes.0.viewId": "whale_testlist", "panes.0.pluginTopItem": "f060.txt"})

        # B: history back and forward restore view and state
        client.tool("set_location", pane=0, url=url_b)
        s.expect("B: folder b, the view stays, from the top", {"url": url_b, "panes.0.loading": False,
                                                                "panes.0.viewId": "whale_testlist", "panes.0.pluginTopItem": "g000.txt"})
        client.tool("set_view", pane=0, view="dolphin")
        s.expect("B: file list in b", {"panes.0.viewId": "dolphin"})
        client.tool("trigger_action", action="go_back")
        s.expect("B: back: test list at f060", {
            "url": url, "panes.0.loading": False, "panes.0.viewId": "whale_testlist", "panes.0.pluginTopItem": "f060.txt",
            "panes.0.restore": {"source": "history", "requested": "whale_testlist", "shown": "whale_testlist",
                                "fallback": False}})
        client.tool("trigger_action", action="go_forward")
        s.expect("B: forward: file list in b", {"url": url_b, "panes.0.viewId": "dolphin",
                                                 "panes.0.restore.requested": "dolphin"})
        client.tool("set_view", pane=0, view="whale_testlist")
        client.tool("scroll_to_item", pane=0, url=(root / "b" / "g030.txt").as_uri())
        s.expect("B: test list in b at g030", {"panes.0.pluginTopItem": "g030.txt"})
        client.tool("trigger_action", action="go_back")
        s.expect("B: back to f060", {"url": url, "panes.0.pluginTopItem": "f060.txt"})
        client.tool("trigger_action", action="go_forward")
        s.expect("B: forward to g030", {"url": url_b, "panes.0.viewId": "whale_testlist", "panes.0.pluginTopItem": "g030.txt"})
        client.tool("trigger_action", action="go_back")
        s.expect("B: and back to the folder", {"url": url, "panes.0.pluginTopItem": "f060.txt"})

        # C: split view, two independent view areas
        client.tool("set_split_view", enabled=True)
        s.expect("C: split", {"splitAnimating": False, "paneCount": 2, "activePane": 1, "panes.1.loading": False})
        client.tool("set_location", pane=1, url=url_b)
        s.expect("C: right in b", {"panes.1.url": url_b, "panes.1.loading": False})
        client.tool("set_view", pane=1, view="whale_testlist")
        client.tool("scroll_to_item", pane=1, url=(root / "b" / "g020.txt").as_uri())
        s.expect("C: right at g020, left still at f060", {"panes.1.pluginTopItem": "g020.txt", "panes.0.pluginTopItem": "f060.txt"})
        client.tool("set_location", pane=1, url=url_zoom)
        s.expect("C: right elsewhere", {"panes.1.url": url_zoom, "panes.1.loading": False})
        client.tool("trigger_action", action="go_back")
        s.expect("C: right back at g020, left untouched", {
            "panes.1.url": url_b, "panes.1.viewId": "whale_testlist", "panes.1.pluginTopItem": "g020.txt",
            "panes.0.viewId": "whale_testlist", "panes.0.pluginTopItem": "f060.txt"})

        # D: session restart with two tabs, a split view and view states
        client.tool("trigger_action", action="new_tab")
        s.expect("D: second tab", {"tabs.count": 2, "tabs.current": 1})
        client.tool("set_location", pane=0, url=url_zoom)
        s.expect("D: second tab in z-zoom, file list", {"url": url_zoom, "panes.0.viewId": "dolphin"})
        w.quit()
        if not session_files(work / "profile"):
            raise Failure("no session with Whale's data was saved")
        w = Session(args, work)
        client = w.client
        s.client = client
        s.expect("D: restored: two tabs, the second one active", {"tabs.count": 2, "tabs.current": 1, "url": url_zoom,
                                                                    "panes.0.viewId": "dolphin"}, timeout_ms=20000)
        # The automation answers before Dolphin has restored its session.
        s.expect("session restored", {"tabs.count": 2}, timeout_ms=20000)
        client.tool("trigger_action", action="activate_prev_tab")
        s.expect("D: first tab: split, both test lists, both positions", {
            "tabs.current": 0, "paneCount": 2, "panes.0.url": url, "panes.1.url": url_b,
            "panes.0.viewId": "whale_testlist", "panes.1.viewId": "whale_testlist",
            "panes.0.pluginTopItem": "f060.txt", "panes.1.pluginTopItem": "g020.txt",
            "panes.0.restore.source": "session", "panes.1.restore.fallback": False}, timeout_ms=20000)

        # E: a session without Whale's data, as Dolphin or an older Whale wrote it
        w.quit()
        for path in session_files(work / "profile"):
            text = path.read_text()
            path.write_text(re.sub(r"(?m)^WhaleViews=.*\n", "", text))
        if session_files(work / "profile"):
            raise Failure("Whale's session data is still there")
        log_size = (work / "profile" / "whale.log").stat().st_size
        w = Session(args, work)
        client = w.client
        s.client = client
        # The automation answers before Dolphin has restored its session. The first tab
        # was the active one when Whale quit.
        s.expect("session restored", {"tabs.count": 2, "tabs.current": 0}, timeout_ms=20000)
        s.expect("E: old session: tabs and split back, file lists", {
            "tabs.count": 2, "tabs.current": 0, "paneCount": 2, "panes.0.url": url, "panes.0.viewId": "dolphin",
            "panes.1.viewId": "dolphin", "panes.0.restore": None, "panes.0.loading": False}, timeout_ms=20000)
        new_log = (work / "profile" / "whale.log").read_text()[log_size:]
        if "Cannot restore" in new_log or "Ignoring" in new_log:
            raise Failure(f"warnings for an old session: {new_log}")

        # F: the plugin is missing: the file list, and the plugin's state is kept
        client.tool("set_view", pane=0, view="whale_testlist")
        client.tool("scroll_to_item", pane=0, url=(root / "f090.txt").as_uri())
        s.expect("F: test list at f090", {"panes.0.pluginTopItem": "f090.txt"})
        w.quit()
        w = Session(args, work, plugins=False)
        client = w.client
        s.client = client
        # The automation answers before Dolphin has restored its session. The first tab
        # was the active one when Whale quit.
        s.expect("session restored", {"tabs.count": 2, "tabs.current": 0}, timeout_ms=20000)
        s.expect("F: without the plugin: file list, state kept", {
            "tabs.current": 0, "panes.0.viewId": "dolphin", "panes.0.loading": False,
            "panes.0.restore": {"source": "session", "requested": "whale_testlist", "shown": "dolphin", "fallback": True},
            "panes.0.storedViewStates": {"$contains": "whale_testlist"}}, timeout_ms=20000)
        # As a user: switch to the side, then navigate. (Dolphin saves the session on changes
        # of the active view; after restoring a split view it only follows the active side
        # once the active side has changed, see plan 14.13.)
        client.tool("activate_pane", pane="right")
        client.tool("activate_pane", pane="left")
        client.tool("set_location", pane=0, url=url_b)
        client.tool("trigger_action", action="go_back")
        s.expect("F: the history still works", {"url": url, "panes.0.loading": False, "panes.0.viewId": "dolphin",
                                                "panes.0.storedViewStates": {"$contains": "whale_testlist"}})
        w.quit()
        w = Session(args, work)
        client = w.client
        s.client = client
        # The automation answers before Dolphin has restored its session. The first tab
        # was the active one when Whale quit.
        s.expect("session restored", {"tabs.count": 2, "tabs.current": 0}, timeout_ms=20000)
        s.expect("F: plugin back: file list as last shown", {"tabs.current": 0, "panes.0.viewId": "dolphin",
                                                            "panes.0.loading": False}, timeout_ms=20000)
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("F: the test list gets its kept state", {"panes.0.viewId": "whale_testlist", "panes.0.pluginTopItem": "f090.txt"})

        # G: capabilities follow the location and a restore from the history
        client.tool("set_split_view", enabled=False)
        client.tool("set_location", pane=0, url=url)
        client.tool("set_view", pane=0, view="whale_test_dynamic")
        s.expect("G: dynamic view, no zoom here", {"paneCount": 1, "panes.0.viewId": "whale_test_dynamic",
                                                    "actions.view_zoom_in.gate": "no Zoom capability"})
        client.tool("set_location", pane=0, url=url_zoom)
        s.expect("G: zoom in z-zoom", {"url": url_zoom, "actions.view_zoom_in.gate": None, "actions.view_zoom_in.enabled": True})
        client.tool("trigger_action", action="go_back")
        s.expect("G: back: no zoom", {"url": url, "actions.view_zoom_in.gate": "no Zoom capability"})
        client.tool("trigger_action", action="go_forward")
        s.expect("G: forward (history restore): zoom again", {"url": url_zoom, "panes.0.viewId": "whale_test_dynamic",
                                                              "actions.view_zoom_in.gate": None})
    finally:
        w.kill()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--whale", required=True)
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--plugin-dir", required=True)
    parser.add_argument("--dynamic-plugin-dir", required=True)
    parser.add_argument("--platform", default="offscreen", help="empty for the session's platform")
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_M6_PRIVATE_BUS") != "1":
        # Run again on a private session bus of our own. Services that bus starts (portal,
        # wallet) inherit its output and may outlive it, so the output goes through a file:
        # a pipe would keep a caller like ctest waiting.
        env = dict(os.environ, WHALE_M6_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
        with tempfile.TemporaryFile() as output:
            code = subprocess.call(["dbus-run-session", "--", sys.executable, *sys.argv], env=env, stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT)
            output.seek(0)
            for line in output.read().decode(errors="replace").splitlines():
                # The private bus' own messages are noise here.
                if not line.startswith(("dbus-daemon[", "kf.wallet", "qt.qpa.services", "A connection to the bus")):
                    print(line)
        return code
    if args.screenshots:
        os.makedirs(args.screenshots, exist_ok=True)

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-m6-"))
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
    print("M6 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
