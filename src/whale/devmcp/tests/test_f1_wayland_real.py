#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""F1 with real input on Plasma/Wayland: what synthesized events cannot show, because
the compositor carries it (wayland_real_input.py: ydotool and a KWin script).

  R1 a real click selects a deep file of the recursive view
  R2 Ctrl+click adds another one (multiple selection)
  R3 a real drag of a deep file out of the recursive view into the other half (copied)
  R4 a real drag from the other half over the recursive view: refused (no drop action,
     no operation), explained once although the pointer keeps moving over it
  R5 back to the current folder: a real drag into it is a normal Dolphin drop again

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


def state(client):
    return json.loads(client.tool("get_ui_state")[0]["text"])


def point(client, pane, path=None):
    """The window position of the item @p path (relative to the location) or of the background."""
    points = state(client)["panes"][pane]["fileListPoints"]
    if path is None:
        return points["background"]["x"], points["background"]["y"]
    for item in points["items"]:
        if item["path"] == path:
            return item["x"], item["y"]
    raise Failure(f"{path} is not shown in pane {pane}")


def hover(client, real, pane, path, url):
    """Moves the real pointer onto the item @p path of @p pane; returns its window position.
    The file list reports a hovered item to the information panel, but not always while
    several items are selected; then KWin's pointer position on the item is the proof."""
    x, y = point(client, pane, path)
    for attempt in range(3):
        real.move_to_window_position(x + 2 * attempt, y)
        deadline = time.time() + 1.0
        while time.time() < deadline:
            if state(client)["shell"]["itemInfoUrl"] == url:
                print(f"   the pointer is on {path} (hovered)")
                return x + 2 * attempt, y
            time.sleep(0.05)
    wx, wy, _, _ = real.prepare_window()
    cx, cy = real.cursor()
    if abs(cx - wx - x - 4) <= 3 and abs(cy - wy - y) <= 3:
        print(f"   the pointer is on {path} (pointer position)")
        return x + 4, y
    raise Failure(f"the pointer is not on {path} in pane {pane} at {x}, {y} (cursor {cx}, {cy}, window {wx}, {wy})")


def run(args, work):
    root = work / "files" / "root"
    for name, size in {"top.txt": 300, "a/one.txt": 100, "a/b/two.png": 5000, "a/b/c/three.txt": 200}.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(b"x" * size)
    target = work / "files" / "target"
    target.mkdir()
    (target / "existing.txt").write_bytes(b"e" * 10)
    r, t = root.as_uri(), target.as_uri()

    whale = start_whale(args.whale, None, work / "profile", args.plugin_dir, "", automation=True, file_operations=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_f1_wayland_real", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)
        real = wayland_real_input.RealInput(whale.pid)

        client.tool("set_window_size", width=1300, height=850)
        client.tool("set_location", pane=0, url=r)
        client.tool("set_hidden_files", pane=0, shown=False)
        client.tool("set_sort", pane=0, field="name", order="ascending", folders_first=True)
        client.tool("trigger_action", action="view_details")
        client.tool("set_content_scope", pane=0, scope="recursive-files")
        s.expect("left: Details with subfolders", {"url": r, "panes.0.contentScope.shown": True, "panes.0.itemCount": 4,
                                                   "panes.0.loading": False})
        client.tool("set_split_view", enabled=True)
        s.expect("split", {"paneCount": 2})
        client.tool("set_location", pane=1, url=t)
        client.tool("set_split_ratio", left_share=0.5)
        s.expect("right: the target folder, current folder", {"panes.1.url": t, "panes.1.loading": False,
                                                              "panes.1.contentScope.shown": False})
        time.sleep(1.0)  # the split animation must be over before real moves
        print("real input: test window at", real.prepare_window())

        # R1: a real click on a deep file
        three = (root / "a/b/c/three.txt").as_uri()
        hover(client, real, 0, "a/b/c/three.txt", three)
        real.click()
        s.expect("R1: clicked: selected, left half active", {"panes.0.selection": [three], "activePane": 0})

        # R2: Ctrl+click
        one = (root / "a/one.txt").as_uri()
        hover(client, real, 0, "a/one.txt", one)
        real.key_down(wayland_real_input.KEY_LEFTCTRL)
        real.click()
        real.key_up(wayland_real_input.KEY_LEFTCTRL)
        s.expect("R2: Ctrl+click: two deep files selected", {"panes.0.selectionCount": 2, "panes.0.selection": {"$contains": one}})

        # R3: drag a deep file out into the other half (Ctrl: copy, no drop menu)
        x, y = hover(client, real, 0, "a/b/two.png", (root / "a/b/two.png").as_uri())
        real.press()
        bx, by = point(client, 1)
        for i in range(1, 9):
            real.move_to_window_position(x + (bx - x) * i / 8, y + (by - y) * i / 8)
        real.key_down(wayland_real_input.KEY_LEFTCTRL)
        time.sleep(0.2)
        real.release()
        real.key_up(wayland_real_input.KEY_LEFTCTRL)
        s.expect("R3: dropped into the right half: copied there", {"panes.1.items": {"$contains": "two.png"}}, timeout_ms=10000)
        if not (root / "a/b/two.png").exists():
            raise Failure("R3: the source file vanished")
        s.expect("R3: no explanation for the view's own drag", {"panes.0.contentScope.refusals": 0})

        # R4: drag from the right half over the recursive view: refused, explained once
        x, y = hover(client, real, 1, "existing.txt", (target / "existing.txt").as_uri())
        real.press()
        bx, by = point(client, 0)
        for i in range(1, 9):
            real.move_to_window_position(x + (bx - x) * i / 8, y + (by - y) * i / 8)
        # Keep moving over the recursive view: the explanation must not repeat.
        for i in range(10):
            real.move_to_window_position(bx - 10 * (i % 3), by - 15 * (i % 2))
        moving = state(client)["panes"][0]["contentScope"]
        real.release()
        if moving["refusals"] != 1:
            raise Failure(f"R4: {moving['refusals']} explanations while dragging over the recursive view, expected 1")
        result = s.expect("R4: refused and explained", {"panes.0.contentScope.refusals": 1,
                                                       "panes.0.contentScope.lastRefusal": {"$contains": ""}})
        if result["panes"][0]["message"] != result["panes"][0]["contentScope"]["lastRefusal"]:
            raise Failure("R4: the explanation is not shown in the view area")
        time.sleep(1.0)
        if any(path.name == "existing.txt" for path in root.rglob("*")):
            raise Failure("R4: the refused drop changed files below the recursive view")
        if not (target / "existing.txt").exists():
            raise Failure("R4: the refused drop moved the source")

        # R5: without the scope, a real drop into the left half is Dolphin's again
        client.tool("set_content_scope", pane=0, scope="current-folder")
        s.expect("R5: left: current folder", {"panes.0.contentScope.shown": False, "panes.0.loading": False})
        time.sleep(0.5)
        x, y = hover(client, real, 1, "existing.txt", (target / "existing.txt").as_uri())
        real.press()
        bx, by = point(client, 0)
        for i in range(1, 9):
            real.move_to_window_position(x + (bx - x) * i / 8, y + (by - y) * i / 8)
        real.key_down(wayland_real_input.KEY_LEFTCTRL)
        time.sleep(0.2)
        real.release()
        real.key_up(wayland_real_input.KEY_LEFTCTRL)
        s.expect("R5: copied into the location", {"panes.0.items": {"$contains": "existing.txt"},
                                                  "panes.0.contentScope.refusals": 1}, timeout_ms=10000)
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
    if not os.environ.get("WAYLAND_DISPLAY") and os.environ.get("WHALE_F1_PRIVATE_BUS") != "1":
        print("skipped: no Wayland session")
        return 77
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_F1_PRIVATE_BUS") != "1":
        env = dict(os.environ, WHALE_F1_PRIVATE_BUS="1", PYTHONUNBUFFERED="1",
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
    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-f1-real-"))
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
    print("F1 real input acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
