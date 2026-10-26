#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of stage V3 of the built-in viewer, videos (.privatedoc/whale-viewer-plan.md),
driven only through the dev MCP. Skipped (77) when Whale is built without libmpv.

  A  opening a video plays it, with sound, over and over; the view follows
  B  a click on the video and Space pause, M mutes, Left/Right seek and do not page
  C  the controls: they show with the pointer and while paused; play button, seeking
  D  paging stops the video, paging back plays it again
  E  the settings: not at once, no sound
  F  a file that does not play: the same message as for images

On the offscreen platform mpv plays without output (player.drawn false).
Arguments as test_m4_acceptance.py.
"""

import argparse
import os
import pathlib
import re
import shutil
import struct
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance

SKIPPED = 77


def test_video():
    """The WebM video of the C++ tests (2 s, 32 × 24, red, with sound), from whaletestvideo.h."""
    header = pathlib.Path(__file__).resolve().parents[2] / "tests" / "whaletestvideo.h"
    escapes = re.findall(r"\\x([0-9a-f]{2})", header.read_text())
    return bytes(int(value, 16) for value in escapes)


def png(width, height, rgb):
    """A PNG of one color, written without any image library."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    row = b"\x00" + bytes(rgb) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))


class Skipped(Exception):
    pass


def run(args, work):
    folder = work / "files"
    folder.mkdir(parents=True)
    (folder / "1.png").write_bytes(png(64, 48, (40, 120, 200)))
    (folder / "2.webm").write_bytes(test_video())
    (folder / "3.png").write_bytes(png(64, 48, (200, 120, 40)))
    (folder / "4.webm").write_bytes(b"not a video")
    profile = work / "profile"
    (profile / "config").mkdir(parents=True)

    whale = start_whale(args.whale, folder, profile, args.plugin_dir, args.platform, automation=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_v3_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)
        uri = lambda name: (folder / name).as_uri()

        state = s.expect("start", {"panes.0.loading": False, "panes.0.itemCount": 4, "viewer.open": False})
        if not state["viewer"]["settings"]["videoSupported"]:
            raise Skipped()

        def viewer(title, conditions, timeout_ms=10000):
            return s.expect(title, {f"viewer.{key}": value for key, value in conditions.items()}, timeout_ms=timeout_ms,
                            focus=False)

        def key(name):
            client.tool("viewer_input", action="key", key=name)

        def position():
            return client.state()["viewer"]["player"]["position"]

        def open_file(name):
            client.tool("set_selection", pane=0, urls=[uri(name)], current=uri(name))
            client.tool("activate_items", pane=0, urls=[uri(name)])

        viewer_settings = {"settings.videoAutoplay": True, "settings.videoSound": True, "settings.videoLoop": True}
        s.expect("defaults", {f"viewer.{key}": value for key, value in viewer_settings.items()})

        # A
        open_file("2.webm")
        viewer("A: the video plays", {"open": True, "name": "2.webm", "video": True, "loaded": True,
                                      "player.file": "2.webm", "player.duration": 2.0, "player.paused": False,
                                      "player.muted": False, "player.size": [32, 24], "player.drawn": False})
        s.expect("A: the view follows", {"panes.0.currentName": "2.webm", "panes.0.selectedNames": ["2.webm"]},
                 focus=False)
        # Round again: the position goes up and then down.
        viewer("A: on", {"player.position": 1.5}, timeout_ms=5000)
        viewer("A: over again", {"player.position": 0.5, "player.atEnd": False}, timeout_ms=5000)
        print("ok    A: plays, over and over")

        # B
        client.tool("viewer_input", action="click", where="center")
        viewer("B: a click on the video pauses", {"player.paused": True, "name": "2.webm"})
        client.tool("viewer_input", action="click", where="center")
        viewer("B: and plays", {"player.paused": False, "name": "2.webm"})
        key("Space")
        viewer("B: Space pauses", {"player.paused": True})
        key("M")
        viewer("B: M mutes", {"player.muted": True})
        key("M")
        viewer("B: M again", {"player.muted": False})
        key("Left")
        viewer("B: Left seeks back to the start", {"player.position": 0.0, "name": "2.webm"})
        key("Right")
        viewer("B: Right seeks, does not page", {"name": "2.webm"})
        print("ok    B: keys")

        # C
        viewer("C: paused, the controls show", {"controlsVisible": True})
        client.tool("viewer_input", action="controls", target="0.75")
        viewer("C: seek to three quarters", {"player.position": 1.5, "player.paused": True})
        client.tool("viewer_input", action="controls", target="play")
        viewer("C: the button plays", {"player.paused": False})
        viewer("C: playing, they hide with the pointer", {"controlsVisible": False}, timeout_ms=5000)
        client.tool("viewer_input", action="pointer")
        viewer("C: the pointer moves, they show", {"controlsVisible": True})
        if args.screenshots:
            client.tool("screenshot", target="viewer", save_path=os.path.join(args.screenshots, "controls.png"))
        print("ok    C: controls")

        # D
        key("Up")
        viewer("D: paging stops it", {"name": "1.png", "video": False, "loaded": True, "player.file": "",
                                      "controlsVisible": False})
        key("Down")
        viewer("D: back, it plays again", {"name": "2.webm", "loaded": True, "player.file": "2.webm",
                                           "player.paused": False})
        key("Escape")
        s.expect("D: closed, the video selected", {"viewer.open": False, "panes.0.selectedNames": ["2.webm"]})
        print("ok    D: paging")

        # E
        client.tool("set_viewer_options", video_autoplay=False, video_sound=False, video_loop=False)
        open_file("2.webm")
        viewer("E: paused, without sound", {"open": True, "loaded": True, "player.paused": True, "player.muted": True,
                                            "player.position": 0.0, "settings.videoAutoplay": False})
        client.tool("viewer_input", action="controls", target="play")
        viewer("E: once: it stops at the end", {"player.atEnd": True, "player.paused": True}, timeout_ms=5000)
        key("Space")
        viewer("E: Space at the end plays from the start", {"player.atEnd": False, "player.paused": False})
        key("Escape")
        s.expect("E: closed", {"viewer.open": False}, focus=False)
        client.tool("set_viewer_options", video_autoplay=True, video_sound=True, video_loop=True)
        print("ok    E: settings")

        # F
        open_file("4.webm")
        viewer("F: does not play", {"open": True, "name": "4.webm", "video": True, "loadError": True, "loaded": False})
        key("Up")
        viewer("F: paging on", {"name": "3.png", "loadError": False, "loaded": True})
        key("Escape")
        print("ok    F: a file that does not play")
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
        return SKIPPED
    if args.screenshots:
        os.makedirs(args.screenshots, exist_ok=True)

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-v3-"))
    try:
        run(args, work)
    except Skipped:
        print("skipped: built without libmpv")
        return SKIPPED
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
