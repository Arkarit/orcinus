#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""The linked split view (LK) with real input on Plasma/Wayland (wayland_real_input.py:
ydotool and a KWin script): what the offscreen acceptance (test_lk_acceptance.py) sets
through the API is done here as the user does it.

  R1 a real click on the lock button links the views (the right view takes the location)
  R2 a real click on an item: selected, and shown in the other view
  R3 Right expands a folder; a real Ctrl+click on an item in it is refused with a message,
     the selection and the current item stay
  R4 a real Shift+click across folders is limited to the folder of the anchor, with a message
  R5 a real click into the other view: it becomes active, its selection goes the other way
  R6 a real click on a deep entry of the treemap: the details view expands the branch and
     selects it
  R7 a real click on the lock button unlinks the views

Only temporary folders are used; Whale runs with a test profile of its own on a private
D-Bus session bus and its window is kept above others while the test runs. Exits with 77
(skipped) without a Wayland session or without ydotoold.
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
import wayland_real_input
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance

TREEMAP = "whale_treemap"
ONE_FOLDER = "While the views are linked, only items of one folder can be selected."
KEY_RIGHT = 106
KEY_LEFTSHIFT = 42


def state(client):
    return json.loads(client.tool("get_ui_state")[0]["text"])


def point(client, pane, path):
    """The window position of the item @p path (relative to the location) of @p pane."""
    points = state(client)["panes"][pane]["fileListPoints"]
    for item in (points or {}).get("items", []):
        if item["path"] == path:
            return item["x"], item["y"]
    raise Failure(f"{path} is not shown in pane {pane}")


def hover(client, real, pane, path, url):
    """Moves the real pointer onto the item @p path of @p pane (Whale reports it hovered)."""
    x, y = point(client, pane, path)
    for attempt in range(3):
        real.move_to_window_position(x + 2 * attempt, y)
        deadline = time.time() + 1.0
        while time.time() < deadline:
            if state(client)["shell"]["itemInfoUrl"] == url:
                return
            time.sleep(0.05)
    wx, wy, _, _ = real.prepare_window()
    cx, cy = real.cursor()
    if abs(cx - wx - x - 4) <= 3 and abs(cy - wy - y) <= 3:
        return
    raise Failure(f"the pointer is not on {path} in pane {pane} at {x}, {y} (cursor {cx}, {cy}, window {wx}, {wy})")


def click_lock_button(client, real):
    lock = state(client)["splitLock"]
    if lock["buttonX"] is None:
        raise Failure("the lock button is not shown")
    real.move_to_window_position(lock["buttonX"], lock["buttonY"])
    time.sleep(0.2)
    real.click()


def run(args, work):
    folder = work / "files"
    for name, size in {"a.txt": 3000, "b.txt": 2000, "other/x.txt": 100, "sub/inner.txt": 4000,
                       "sub/deeper/deep.txt": 5000}.items():
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        (folder / name).write_bytes(b"x" * size)

    def url(path=""):
        return (folder / path).as_uri() if path else folder.as_uri()

    whale = start_whale(args.whale, None, work / "profile", args.plugin_dir, "", automation=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_lk_wayland_real", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)
        real = wayland_real_input.RealInput(whale.pid)

        client.tool("set_window_size", width=1300, height=850)
        client.tool("set_location", pane=0, url=url())
        client.tool("set_hidden_files", pane=0, shown=False)
        client.tool("set_sort", pane=0, field="name", order="ascending", folders_first=True)
        client.tool("trigger_action", action="view_details")
        s.expect("left: Details", {"url": url(), "panes.0.viewMode": "details", "panes.0.loading": False,
                                   "panes.0.items": ["other", "sub", "a.txt", "b.txt"]})
        client.tool("set_split_view", enabled=True)
        s.expect("split", {"splitAnimating": False, "paneCount": 2})
        client.tool("set_location", pane=1, url=url("other"))
        client.tool("set_split_ratio", left_share=0.5)
        client.tool("activate_pane", pane="right")
        client.tool("trigger_action", action="view_icons")
        s.expect("right: another folder, icons", {"panes.1.url": url("other"), "panes.1.loading": False,
                                                  "panes.1.viewMode": "icons", "splitLock.buttonVisible": True})
        client.tool("activate_pane", pane="left")
        s.expect("left active", {"activePane": 0})
        time.sleep(1.0)  # the split animation must be over before real moves
        print("real input: test window at", real.prepare_window())

        # R1: the lock button
        click_lock_button(client, real)
        s.expect("R1: linked by a real click, the right view took the location", {
            "splitLock.locked": True, "splitLock.buttonIcon": "link", "panes.1.url": url(), "panes.1.loading": False,
            "panes.1.viewMode": "icons", "panes.0.viewMode": "details"})

        # R2: a real click on an item
        hover(client, real, 0, "b.txt", url("b.txt"))
        real.click()
        s.expect("R2: selected, shown on the right", {
            "activePane": 0, "panes.0.selectedNames": ["b.txt"], "panes.1.selectedNames": ["b.txt"],
            "panes.1.currentName": "b.txt"})

        # R3: expand sub with Right, then a refused Ctrl+click on an item in it
        hover(client, real, 0, "sub", url("sub"))
        real.click()
        s.expect("R3: sub selected", {"panes.0.selectedNames": ["sub"], "panes.1.selectedNames": ["sub"]})
        real.key(KEY_RIGHT)
        s.expect("R3: sub expanded", {"panes.0.items": ["other", "sub", "deeper", "inner.txt", "a.txt", "b.txt"]})
        time.sleep(0.3)
        hover(client, real, 0, "b.txt", url("b.txt"))
        real.click()
        s.expect("R3: b.txt selected again", {"panes.0.selectedNames": ["b.txt"], "panes.0.currentName": "b.txt"})
        hover(client, real, 0, "sub/inner.txt", url("sub/inner.txt"))
        real.key_down(wayland_real_input.KEY_LEFTCTRL)
        real.click()
        real.key_up(wayland_real_input.KEY_LEFTCTRL)
        s.expect("R3: Ctrl+click in another folder refused, with the message", {
            "panes.0.selectedNames": ["b.txt"], "panes.0.currentName": "b.txt", "panes.0.message": ONE_FOLDER,
            "panes.1.selectedNames": ["b.txt"]})

        # R4: Shift+click across folders, from b.txt up to other. The message of R3 slides in
        # above the file list and moves its items: wait until it has settled.
        time.sleep(1.0)
        hover(client, real, 0, "other", url("other"))
        real.key_down(KEY_LEFTSHIFT)
        real.click()
        real.key_up(KEY_LEFTSHIFT)
        s.expect("R4: Shift+click limited to the folder of the anchor", {
            "panes.0.selectedNames": ["other", "sub", "a.txt", "b.txt"], "panes.0.message": ONE_FOLDER,
            "panes.1.selectedNames": ["other", "sub", "a.txt", "b.txt"]})

        # R5: a real click into the right view: it is active, its selection goes left
        hover(client, real, 1, "a.txt", url("a.txt"))
        real.click()
        s.expect("R5: right active, its selection on the left", {
            "activePane": 1, "panes.1.selectedNames": ["a.txt"], "panes.0.selectedNames": ["a.txt"],
            "panes.0.currentName": "a.txt"})

        # R6: the treemap on the right, a real click on a deep entry
        client.tool("set_view", pane=1, view=TREEMAP)
        s.expect("R6: treemap shown, still linked", {"panes.1.viewId": TREEMAP, "panes.1.treemap.status": "ready",
                                                     "splitLock.locked": True})
        time.sleep(0.5)  # the layout of the new view must have settled
        deep = url("sub/deeper/deep.txt")
        where = json.loads(client.tool("treemap_hover", pane=1, url=deep)[0]["text"])
        client.tool("treemap_hover", pane=1)
        real.move_to_window_position(where["windowX"], where["windowY"])
        s.expect("R6: real pointer on the deep entry", {"panes.1.treemap.hovered": deep}, timeout_ms=3000)
        real.click()
        s.expect("R6: the left view expanded the branch and selected the deep entry", {
            "activePane": 1, "panes.1.treemap.selected": deep, "panes.0.selection": [deep],
            "panes.0.currentName": "deep.txt"})

        # R7: unlink
        click_lock_button(client, real)
        s.expect("R7: unlinked by a real click", {"splitLock.locked": False, "splitLock.buttonIcon": "remove-link",
                                                  "splitLock.panes.0.selectionLimited": False})
        real.release_window()
        client.tool("quit")
        whale.wait(timeout=20)
    finally:
        client.close()
        if whale.poll() is None:
            whale.terminate()
            whale.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--whale", required=True, help="the whale binary of the build tree")
    parser.add_argument("--adapter", required=True, help="whale_dev_mcp.py")
    parser.add_argument("--plugin-dir", required=True, help="the view plugin directories (separated by ':')")
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not os.environ.get("WAYLAND_DISPLAY") and os.environ.get("WHALE_LK_PRIVATE_BUS") != "1":
        print("skipped: no Wayland session")
        return 77
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_LK_PRIVATE_BUS") != "1":
        env = dict(os.environ, WHALE_LK_PRIVATE_BUS="1", PYTHONUNBUFFERED="1",
                   WHALE_SESSION_BUS_ADDRESS=os.environ.get("DBUS_SESSION_BUS_ADDRESS", ""))
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
    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-lk-real-"))
    try:
        run(args, work)
    except wayland_real_input.RealInputUnavailable as reason:
        print(f"skipped: no real input ({reason})")
        return 77
    except Failure as failure:
        print(f"FAILED: {failure}")
        print(f"(Whale log: {work / 'profile' / 'whale.log'})")
        args.keep = True
        return 1
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
    print("LK real input acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
