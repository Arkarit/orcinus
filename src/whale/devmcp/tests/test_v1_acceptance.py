#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the built-in viewer (V1, .privatedoc/whale-viewer-plan.md), driven only
through the dev MCP:

  A  opening an image opens the viewer with the images of its folder, the view follows
  B  paging with keys, wheel and clicks into the edges; the view follows every file
  C  the note at the end without wrap-around, going round with it
  D  a file that does not load; Escape closes, the file shown last is selected
  E  a multiple selection: only its images, the selection stays (A) or moves (B)
  F  more than five images open without the question "open %1 items?"
  G  with no image type for the viewer, images open with their application again

Arguments as test_m4_acceptance.py.
"""

import argparse
import base64
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

IMAGES = [f"{i}.png" for i in range(1, 8)]


def png(width, height, rgb):
    """A small PNG of one color, written without any image library."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    row = b"\x00" + bytes(rgb) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))


def install_image_opener(profile, log):
    """PNG files open with a test "application" that only notes their path in @p log."""
    applications = profile / "data" / "applications"
    applications.mkdir(parents=True, exist_ok=True)
    (applications / "whale-test-image-opener.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=Whale Test Image Opener\nMimeType=image/png;\n"
        f"Exec=sh -c 'echo \"$1\" >> {log}' sh %f\n")
    (profile / "config" / "mimeapps.list").write_text(
        "[Default Applications]\nimage/png=whale-test-image-opener.desktop\n")


def run(args, work):
    folder = work / "files"
    folder.mkdir(parents=True)
    for index, name in enumerate(IMAGES):
        (folder / name).write_bytes(png(64, 48, (40 * index, 120, 200)))
    (folder / "broken.png").write_bytes(b"not an image")
    (folder / "notes.txt").write_bytes(b"text")
    profile = work / "profile"
    (profile / "config").mkdir(parents=True)
    opener_log = work / "opened.log"
    install_image_opener(profile, opener_log)

    whale = start_whale(args.whale, folder, profile, args.plugin_dir, args.platform, automation=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_v1_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)
        uri = lambda name: (folder / name).as_uri()

        def viewer(title, conditions):
            # While the viewer is open, it has the keyboard focus, not the main window.
            return s.expect(title, {f"viewer.{key}": value for key, value in conditions.items()}, focus=False)

        def key(name):
            client.tool("viewer_input", action="key", key=name)

        state = s.expect("start", {"panes.0.loading": False, "panes.0.itemCount": 9, "viewer.open": False,
                                   "viewer.settings.wrapAround": False, "viewer.settings.multiSelection": "keep"})
        # Also video/* when built with libmpv (V3).
        if state["viewer"]["settings"]["mimeTypes"][:1] != ["image/*"]:
            raise Failure(f"start: MIME types {state['viewer']['settings']['mimeTypes']}")

        # A
        client.tool("set_selection", pane=0, urls=[uri("3.png")], current=uri("3.png"))
        client.tool("activate_items", pane=0, urls=[uri("3.png")])
        viewer("A: open on 3.png", {"open": True, "name": "3.png", "index": 2, "count": 8, "fromSelection": False,
                                    "movesSelection": True, "visible": True, "fullScreen": True, "loaded": True,
                                    "onWindowScreen": True, "overlay": "3.png"})
        state = client.state()
        if state["viewer"]["files"] != IMAGES + ["broken.png"]:
            raise Failure(f"A: files {state['viewer']['files']}")
        if args.screenshots:
            client.tool("screenshot", target="viewer", save_path=os.path.join(args.screenshots, "viewer.png"))
        print("ok    A: the images of the folder, not the text file")

        # B
        key("Right")
        viewer("B: Right", {"name": "4.png", "loaded": True})
        s.expect("B: the view follows", {"panes.0.currentName": "4.png", "panes.0.selectedNames": ["4.png"]}, focus=False)
        client.tool("viewer_input", action="wheel", delta=-240)
        viewer("B: two notches down", {"name": "6.png", "loaded": True})
        s.expect("B: the view follows the wheel", {"panes.0.selectedNames": ["6.png"]}, focus=False)
        client.tool("viewer_input", action="click", where="left")
        viewer("B: click into the left edge", {"name": "5.png"})
        client.tool("viewer_input", action="click", where="center")
        viewer("B: a click in the middle does nothing", {"name": "5.png"})
        client.tool("viewer_input", action="click", where="right")
        key("PgDown")
        viewer("B: right edge, Page Down", {"name": "7.png"})
        key("Home")
        viewer("B: Home", {"name": "1.png", "index": 0})
        print("ok    B: paging, the view follows")

        # C
        key("Left")
        viewer("C: before the first file", {"name": "1.png", "overlay": "First file"})
        client.tool("set_viewer_options", wrap_around=True)
        key("Left")
        viewer("C: round to the last file", {"name": "broken.png", "settings.wrapAround": True})
        client.tool("set_viewer_options", wrap_around=False)
        print("ok    C: end of the list, wrap-around")

        # D
        viewer("D: broken.png does not load", {"name": "broken.png", "loadError": True, "loaded": False})
        key("Up")
        viewer("D: back to an image", {"name": "7.png", "loadError": False, "loaded": True})
        key("Escape")
        s.expect("D: closed, 7.png selected", {"viewer.open": False, "panes.0.selectedNames": ["7.png"],
                                               "panes.0.currentName": "7.png"})
        print("ok    D: load error, Escape")

        # E
        selection = ["2.png", "4.png", "6.png", "notes.txt"]
        client.tool("set_selection", pane=0, urls=[uri(n) for n in selection], current=uri("4.png"))
        client.tool("activate_items", pane=0, urls=[uri(n) for n in ["2.png", "4.png", "6.png"]])
        viewer("E: only the selected images", {"open": True, "count": 3, "fromSelection": True, "name": "4.png",
                                               "movesSelection": False, "files": ["2.png", "4.png", "6.png"]})
        key("Right")
        viewer("E: on", {"name": "6.png"})
        s.expect("E: A keeps the selection", {"panes.0.selectedNames": selection, "panes.0.currentName": "6.png"},
                 focus=False)
        key("Escape")
        s.expect("E: closed", {"viewer.open": False}, focus=False)

        client.tool("set_viewer_options", multi_selection="move")
        client.tool("set_selection", pane=0, urls=[uri(n) for n in selection], current=uri("2.png"))
        client.tool("activate_items", pane=0, urls=[uri(n) for n in ["2.png", "4.png", "6.png"]])
        viewer("E: B", {"open": True, "name": "2.png", "movesSelection": True, "settings.multiSelection": "move"})
        key("End")
        key("Escape")
        s.expect("E: B moves the selection", {"viewer.open": False, "panes.0.selectedNames": ["6.png"]}, focus=False)
        client.tool("set_viewer_options", multi_selection="keep")
        print("ok    E: multiple selection A and B")

        # F
        client.tool("set_selection", pane=0, urls=[uri(n) for n in IMAGES], current=uri("1.png"))
        client.tool("activate_items", pane=0, urls=[uri(n) for n in IMAGES])
        viewer("F: seven images without a question", {"open": True, "count": 7})
        if client.state()["dialogs"]:
            raise Failure(f"F: a dialog: {client.state()['dialogs']}")
        key("Escape")
        s.expect("F: closed", {"viewer.open": False}, focus=False)
        print("ok    F: no question")

        # G
        client.tool("set_viewer_options", mime_types=[])
        client.tool("activate_items", pane=0, urls=[uri("1.png")])
        deadline = time.monotonic() + 10
        while not (opener_log.exists() and "1.png" in opener_log.read_text()):
            if time.monotonic() > deadline:
                raise Failure("G: 1.png did not open with its application")
            time.sleep(0.1)
        if client.state()["viewer"]["open"]:
            raise Failure("G: the viewer opened")
        client.tool("set_viewer_options", mime_types=["image/*"])
        print("ok    G: with no image type, the application opens it")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-v1-"))
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
