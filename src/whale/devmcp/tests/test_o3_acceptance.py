#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of O3 (the preferred view of a location), driven only through the
dev MCP; restarts of Whale are done here, as a user would start it again.

  A simple: chosen at A, back at A by a new navigation   B two folders, alternating
  C a location the view does not support (trash:/)       D history before preference
  E session restart before preference, then preference   F split view, crossing over
  G missing plugin: fallback, preference kept, back       H clear the preference

The view is chosen through its View menu entry (set_view with remember), like the user.
Whale runs on a private D-Bus session bus (the script starts one): Dolphin restores its
session only when it is the only instance. Arguments as test_m6_acceptance.py.
"""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance

TREEMAP = "whale_treemap"


class Session:
    """One Whale process with its MCP client, in the profile that survives restarts."""

    def __init__(self, args, work):
        self.whale = start_whale(args.whale, None, work / "profile", args.plugin_dir, args.platform, automation=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_o3_acceptance", "version": "1"}})
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


def preferred(client, url):
    return json.loads(client.tool("get_preferred_view", url=url)[0]["text"])["view"]


def run(args, work):
    root = work / "files"
    for name, size in (("a", 40000), ("b", 2000), ("c", 1000)):
        (root / name).mkdir(parents=True)
        (root / name / f"{name}.bin").write_bytes(b"x" * size)
    (root / "a" / "sub").mkdir()
    (root / "a" / "sub" / "s.txt").write_bytes(b"s" * 3000)
    a = (root / "a").as_uri()
    b = (root / "b").as_uri()
    c = (root / "c").as_uri()
    trash = "trash:/"

    w = Session(args, work)
    try:
        s = Scenario(w.client, args.screenshots)
        client = w.client

        # A: simple
        client.tool("set_location", pane=0, url=a)
        s.expect("A: folder a, no preference: the file list", {
            "url": a, "panes.0.loading": False, "panes.0.viewId": "dolphin", "panes.0.preferredView": None})
        client.tool("set_view", pane=0, view=TREEMAP, remember=True)
        s.expect("A: treemap chosen in the View menu, remembered", {
            "panes.0.viewId": TREEMAP, "panes.0.preferredView": TREEMAP, "panes.0.viewOrigin": "user",
            "panes.0.treemap.status": "ready"})
        client.tool("set_location", pane=0, url=b)
        s.expect("A: folder b without a preference: the treemap stays, nothing remembered", {
            "url": b, "panes.0.viewId": TREEMAP, "panes.0.viewOrigin": "kept", "panes.0.preferredView": None})
        client.tool("set_view", pane=0, view="dolphin", remember=True)
        s.expect("A: file list chosen in b", {"panes.0.viewId": "dolphin", "panes.0.preferredView": "dolphin"})
        client.tool("set_location", pane=0, url=a)
        s.expect("A: a again (new navigation): the treemap by preference", {
            "url": a, "panes.0.viewId": TREEMAP, "panes.0.viewOrigin": "preference",
            "panes.0.restore": {"source": "preference", "requested": TREEMAP, "shown": TREEMAP, "fallback": False},
            "panes.0.treemap.status": "ready"})

        # B: two folders, alternating
        for i in range(3):
            client.tool("set_location", pane=0, url=b)
            s.expect(f"B{i}: b: file list", {"url": b, "panes.0.viewId": "dolphin", "panes.0.viewOrigin": "preference"})
            client.tool("set_location", pane=0, url=a)
            s.expect(f"B{i}: a: treemap", {"url": a, "panes.0.viewId": TREEMAP, "panes.0.viewOrigin": "preference",
                                            "panes.0.treemap.status": "ready"})
        # A subfolder has no preference of its own (no inheritance); the treemap stays.
        client.tool("set_location", pane=0, url=(root / "a" / "sub").as_uri())
        s.expect("B: a/sub: kept, not remembered", {"panes.0.viewId": TREEMAP, "panes.0.viewOrigin": "kept",
                                                     "panes.0.preferredView": None})
        client.tool("set_location", pane=0, url=a)
        s.expect("B: a again", {"url": a, "panes.0.viewId": TREEMAP})

        # C: a location the treemap does not support
        client.tool("set_location", pane=0, url=trash)
        s.expect("C: trash: the file list as fallback", {
            "url": trash, "panes.0.viewId": "dolphin", "panes.0.viewOrigin": "fallback",
            "panes.0.restore.source": "location", "panes.0.preferredView": None})
        if preferred(client, trash) is not None:
            raise Failure("the fallback in the trash was remembered")
        if client.tool_error("set_view", pane=0, view=TREEMAP, remember=True) is None:
            raise Failure("the treemap was offered in the trash")
        client.tool("set_location", pane=0, url=a)
        s.expect("C: a again: the treemap", {"url": a, "panes.0.viewId": TREEMAP, "panes.0.viewOrigin": "preference",
                                              "panes.0.treemap.status": "ready"})

        # D: the history has priority over the preference
        client.tool("set_location", pane=0, url=b)
        s.expect("D: b: file list", {"url": b, "panes.0.viewId": "dolphin"})
        client.tool("set_preferred_view", url=a, view="dolphin")  # as if chosen in another window
        client.tool("trigger_action", action="go_back")
        s.expect("D: back to a: the treemap as it was", {
            "url": a, "panes.0.viewId": TREEMAP, "panes.0.viewOrigin": "history", "panes.0.preferredView": "dolphin",
            "panes.0.restore.source": "history"})
        client.tool("trigger_action", action="go_forward")
        s.expect("D: forward to b: file list", {"url": b, "panes.0.viewId": "dolphin", "panes.0.viewOrigin": "history"})
        client.tool("set_location", pane=0, url=a)
        s.expect("D: a by a new navigation: the preference (file list)", {
            "url": a, "panes.0.viewId": "dolphin", "panes.0.viewOrigin": "preference"})
        client.tool("set_view", pane=0, view=TREEMAP, remember=True)
        s.expect("D: treemap chosen again in a", {"panes.0.viewId": TREEMAP, "panes.0.preferredView": TREEMAP})

        # E: session restart; the session's view has priority, the preferences persist
        client.tool("set_preferred_view", url=a, view="dolphin")
        s.expect("E: a shows the treemap, prefers the file list", {
            "url": a, "panes.0.viewId": TREEMAP, "panes.0.preferredView": "dolphin"})
        w.quit()
        w = Session(args, work)
        client = w.client
        s.client = client
        # The automation answers before Dolphin has restored its session.
        s.expect("E: restored: a with the treemap of the session", {
            "url": a, "panes.0.viewId": TREEMAP, "panes.0.viewOrigin": "session", "panes.0.preferredView": "dolphin",
            "panes.0.treemap.status": "ready"}, timeout_ms=20000)
        if preferred(client, b) != "dolphin":
            raise Failure("the preference of b did not survive the restart")
        client.tool("set_location", pane=0, url=c)
        s.expect("E: c", {"url": c, "panes.0.loading": False})
        client.tool("set_location", pane=0, url=a)
        s.expect("E: a by a new navigation: the preference", {
            "url": a, "panes.0.viewId": "dolphin", "panes.0.viewOrigin": "preference"})
        client.tool("set_view", pane=0, view=TREEMAP, remember=True)
        s.expect("E: treemap chosen again", {"panes.0.viewId": TREEMAP, "panes.0.preferredView": TREEMAP})

        # F: split view; the preferences belong to the locations, not to the sides
        client.tool("set_split_view", enabled=True)
        s.expect("F: split: the new right side at a shows a's treemap", {
            "paneCount": 2, "activePane": 1, "panes.1.url": a, "panes.1.viewId": TREEMAP,
            "panes.1.viewOrigin": "preference", "panes.1.treemap.status": "ready"})
        client.tool("set_location", pane=1, url=b)
        s.expect("F: right in b: file list", {"panes.1.url": b, "panes.1.viewId": "dolphin", "panes.0.viewId": TREEMAP})
        client.tool("set_location", pane=0, url=b)
        client.tool("set_location", pane=1, url=a)
        s.expect("F: crossed: left b file list, right a treemap", {
            "panes.0.url": b, "panes.0.viewId": "dolphin", "panes.1.url": a, "panes.1.viewId": TREEMAP,
            "panes.1.treemap.status": "ready"})
        client.tool("set_location", pane=0, url=a)
        client.tool("set_location", pane=1, url=b)
        s.expect("F: crossed back", {"panes.0.url": a, "panes.0.viewId": TREEMAP, "panes.1.url": b,
                                     "panes.1.viewId": "dolphin"})
        client.tool("set_split_view", enabled=False)
        s.expect("F: split closed", {"paneCount": 1})

        # G: the plugin goes missing and comes back. A treemap that is shown stays (its plugin
        # is loaded); a new one cannot be made, so start from b's file list.
        client.tool("set_location", pane=0, url=b)
        s.expect("G: b: file list", {"url": b, "panes.0.viewId": "dolphin", "panes.0.loading": False})
        log_size = (work / "profile" / "whale.log").stat().st_size
        client.tool("set_view_plugin_available", view=TREEMAP, available=False)
        for i in range(2):
            client.tool("set_location", pane=0, url=a)
            s.expect(f"G{i}: a without the plugin: file list, preference kept", {
                "url": a, "panes.0.loading": False, "panes.0.viewId": "dolphin", "panes.0.viewOrigin": "fallback",
                "panes.0.preferredView": TREEMAP,
                "panes.0.restore": {"source": "preference", "requested": TREEMAP, "shown": "dolphin", "fallback": True}})
            client.tool("set_location", pane=0, url=c)
            s.expect(f"G{i}: c: the file list stays", {"url": c, "panes.0.viewId": "dolphin"})
        new_log = (work / "profile" / "whale.log").read_text()[log_size:]
        if any(text in new_log for text in ("Cannot restore", "No compatible view plugin", "Could not load view plugin")):
            raise Failure(f"warnings for the missing plugin: {new_log}")
        client.tool("set_view_plugin_available", view=TREEMAP, available=True)
        client.tool("set_location", pane=0, url=a)
        s.expect("G: plugin back: a shows the treemap again", {
            "url": a, "panes.0.viewId": TREEMAP, "panes.0.viewOrigin": "preference", "panes.0.treemap.status": "ready"})

        # H: clear the preference
        client.tool("clear_preferred_view", url=a)
        client.tool("set_location", pane=0, url=b)
        s.expect("H: b: file list", {"url": b, "panes.0.viewId": "dolphin"})
        client.tool("set_location", pane=0, url=a)
        s.expect("H: a without a preference: the default (the shown file list stays)", {
            "url": a, "panes.0.viewId": "dolphin", "panes.0.viewOrigin": "kept", "panes.0.preferredView": None})
        w.quit()
    except BaseException:
        w.kill()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--whale", required=True, help="the whale binary of the build tree")
    parser.add_argument("--adapter", required=True, help="whale_dev_mcp.py")
    parser.add_argument("--plugin-dir", required=True, help="build tree's view plugins (plugins/)")
    parser.add_argument("--platform", default="offscreen", help='QT_QPA_PLATFORM for Whale ("" = the session\'s)')
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_O3_PRIVATE_BUS") != "1":
        # Run again on a private session bus of our own (see test_m6_acceptance.py).
        env = dict(os.environ, WHALE_O3_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-o3-"))
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
    print("O3 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
