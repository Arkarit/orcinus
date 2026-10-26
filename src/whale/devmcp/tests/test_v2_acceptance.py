#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of stage V2 of the built-in viewer (.privatedoc/whale-viewer-plan.md), driven
only through the dev MCP:

  A  a large image fits; the neighbours are decoded ahead (one back, two ahead)
  B  zoom: 1 for 100 %, Ctrl+wheel, +/-, dragging with the middle button, 0 fits again
  C  paging fits again; paging back turns the preloading round
  D  an animated GIF plays and stops when paging on
  E  the settings "enlarge small images" and "smooth pixels"

Arguments as test_m4_acceptance.py.
"""

import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile
import time
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance

# A 4 × 4 GIF of three frames (red, blue, green), 30 ms each, looping.
ANIMATED_GIF = bytes.fromhex(
    "47494638396104000400810000ff000000000000000000000021ff0b4e45545343415045322e30030100000021f90400"
    "030000002c00000000040004000008090001081c48b02080800021f90401030001002c0000000004000400810000ff00"
    "000000000000000008090001081c48b02080800021f90401030001002c00000000040004008100ff0000000000000000"
    "000008090001081c48b0208080003b")


def png(width, height, rgb):
    """A PNG of one color, written without any image library."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    row = b"\x00" + bytes(rgb) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))


def run(args, work):
    folder = work / "files"
    folder.mkdir(parents=True)
    (folder / "1.png").write_bytes(png(64, 48, (200, 40, 40)))
    (folder / "2.png").write_bytes(png(3000, 2000, (40, 120, 200)))
    for index, name in enumerate(["3.png", "4.png", "5.png"]):
        (folder / name).write_bytes(png(64, 48, (40 * index, 200, 40)))
    (folder / "6.gif").write_bytes(ANIMATED_GIF)
    profile = work / "profile"
    (profile / "config").mkdir(parents=True)

    whale = start_whale(args.whale, folder, profile, args.plugin_dir, args.platform, automation=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_v2_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)
        uri = lambda name: (folder / name).as_uri()

        def viewer(title, conditions):
            # While the viewer is open, it has the keyboard focus, not the main window.
            return s.expect(title, {f"viewer.{key}": value for key, value in conditions.items()}, focus=False)

        def key(name):
            client.tool("viewer_input", action="key", key=name)

        def open_file(name):
            client.tool("set_selection", pane=0, urls=[uri(name)], current=uri(name))
            client.tool("activate_items", pane=0, urls=[uri(name)])

        s.expect("start", {"panes.0.loading": False, "panes.0.itemCount": 6, "viewer.open": False,
                           "viewer.settings.upscale": False, "viewer.settings.smoothZoom": False})

        # A
        open_file("2.png")
        state = viewer("A: the large image", {"open": True, "name": "2.png", "loaded": True, "zoomed": False,
                                              "imageSize": [3000, 2000], "visibleRect": [0, 0, 3000, 2000]})
        fitted = state["viewer"]["zoom"]
        if not 0 < fitted < 1:
            raise Failure(f"A: fitted zoom {fitted}")
        viewer("A: one back, two ahead", {"cached": ["1.png", "2.png", "3.png", "4.png"]})
        if args.screenshots:
            client.tool("screenshot", target="viewer", save_path=os.path.join(args.screenshots, "fitted.png"))
        print("ok    A: fitted, neighbours decoded ahead")

        # B
        key("1")
        viewer("B: 100 %", {"zoomed": True, "zoom": 1.0})
        client.tool("viewer_input", action="zoom_wheel", delta=120, at=[0.25, 0.25])
        state = viewer("B: Ctrl+wheel", {"zoomed": True, "zoom": 1.25, "name": "2.png"})
        before = state["viewer"]["visibleRect"]
        client.tool("viewer_input", action="drag", dx=100, dy=40)
        state = client.state()
        after = state["viewer"]["visibleRect"]
        # Dragged right and down: more of the left and top shows (window pixels / zoom, dpr 1 here).
        expected = [before[0] - 80, before[1] - 32]
        if abs(after[0] - expected[0]) > 1 or abs(after[1] - expected[1]) > 1:
            raise Failure(f"B: dragged from {before} to {after}, expected the corner at {expected}")
        key("+")
        key("-")
        viewer("B: + and -", {"zoom": 1.25})
        key("0")
        viewer("B: 0 fits again", {"zoomed": False, "zoom": fitted, "visibleRect": [0, 0, 3000, 2000]})
        if args.screenshots:
            key("1")
            client.tool("screenshot", target="viewer", save_path=os.path.join(args.screenshots, "zoomed.png"))
        print("ok    B: zoom and drag")

        # C
        key("1")
        viewer("C: zoomed", {"zoomed": True})
        key("Right")
        viewer("C: paging fits again", {"name": "3.png", "zoomed": False, "loaded": True})
        viewer("C: ahead", {"cached": ["2.png", "3.png", "4.png", "5.png"]})
        key("Left")
        viewer("C: back", {"name": "2.png", "loaded": True, "zoomed": False})
        viewer("C: turned round", {"cached": ["1.png", "2.png", "3.png"]})
        print("ok    C: paging fits, preloading follows the direction")

        # D
        key("End")
        viewer("D: the animation plays", {"name": "6.gif", "loaded": True, "animated": True, "animating": True})
        frames = set()
        deadline = time.monotonic() + 5
        while len(frames) < 3:
            if time.monotonic() > deadline:
                raise Failure(f"D: frames seen {sorted(frames)}")
            frames.add(client.state()["viewer"]["animationFrame"])
        key("Left")
        viewer("D: paging on stops it", {"name": "5.png", "animating": False, "animationFrame": -1, "animated": False})
        print("ok    D: animated GIF")

        # E
        key("Escape")
        s.expect("E: closed", {"viewer.open": False}, focus=False)
        client.tool("set_viewer_options", upscale=True, smooth_zoom=True)
        open_file("1.png")
        state = viewer("E: a small image fills the screen", {"open": True, "name": "1.png", "loaded": True,
                                                             "zoomed": False, "settings.upscale": True,
                                                             "settings.smoothZoom": True})
        if state["viewer"]["zoom"] <= 1:
            raise Failure(f"E: enlarged zoom {state['viewer']['zoom']}")
        key("Escape")
        client.tool("set_viewer_options", upscale=False, smooth_zoom=False)
        open_file("1.png")
        viewer("E: not enlarged", {"open": True, "name": "1.png", "loaded": True, "zoom": 1.0,
                                   "settings.upscale": False, "settings.smoothZoom": False})
        key("Escape")
        print("ok    E: enlarge small images, smooth pixels")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-v2-"))
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
    print("passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
