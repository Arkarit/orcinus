#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Videos of the built-in viewer really drawn (V3d, .privatedoc/whale-viewer-plan.md): the
OpenGL path that the offscreen tests cannot reach. Runs Whale on an invisible KWin of its own
(kwin_wayland --virtual) on a D-Bus session of its own, so it touches neither the desktop nor
its KWin; the sound is off. Not part of ctest (needs kwin_wayland and Pillow); run by hand:

  python3 test_v3_render_virtual.py --whale ../build-whale/bin/orcinus --adapter ../adapter/whale_dev_mcp.py
      --plugin-dir ../build-whale/bin [--screenshots DIR]

Checks: the video is drawn (red in the middle of a screenshot of the viewer), the controls are
drawn over it, paging to an image hides the video.
"""

import argparse
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance
from test_v3_acceptance import png, test_video

SKIPPED = 77
PRIVATE_BUS = "WHALE_V3_RENDER_PRIVATE_BUS"
# A session bus that starts no services: portals or a wallet started on it would outlive it.
BUS_CONFIG = """<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
"""


def screenshot(client, path):
    import base64
    from PIL import Image
    data = base64.b64decode(client.tool("screenshot", target="viewer", save_path=str(path) if path else None)[0]["data"])
    return Image.open(io.BytesIO(data)).convert("RGB")


def start_kwin(work):
    socket = f"whale-v3-{os.getpid()}"
    log = open(work / "kwin.log", "w")
    kwin = subprocess.Popen(["kwin_wayland", "--virtual", "--no-lockscreen", "--socket", socket,
                             "--width", "1024", "--height", "768"], stdout=log, stderr=subprocess.STDOUT)
    runtime = pathlib.Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp"))
    deadline = time.monotonic() + 15
    while not (runtime / socket).exists():
        if time.monotonic() > deadline or kwin.poll() is not None:
            kwin.kill()
            raise Failure(f"kwin_wayland --virtual did not start (log: {work / 'kwin.log'})")
        time.sleep(0.1)
    return kwin, socket


def run(args, work):
    folder = work / "files"
    folder.mkdir(parents=True)
    (folder / "1.png").write_bytes(png(64, 48, (40, 120, 200)))
    (folder / "2.webm").write_bytes(test_video())
    profile = work / "profile"
    (profile / "config").mkdir(parents=True)
    # No sound from the speakers.
    (profile / "config" / "orcinusrc").write_text("[Viewer]\nVideoSound=false\n")

    kwin, socket = start_kwin(work)
    os.environ["WAYLAND_DISPLAY"] = socket
    os.environ.pop("DISPLAY", None)
    whale = start_whale(args.whale, folder, profile, args.plugin_dir, "wayland", automation=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_v3_render_virtual", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, None)
        s.require_keyboard_focus = False
        uri = lambda name: (folder / name).as_uri()

        def viewer(title, conditions, timeout_ms=10000):
            return s.expect(title, {f"viewer.{key}": value for key, value in conditions.items()}, timeout_ms=timeout_ms,
                            focus=False)

        s.expect("start", {"panes.0.loading": False, "panes.0.itemCount": 2, "viewer.settings.videoSupported": True,
                           "viewer.settings.videoSound": False})
        client.tool("set_selection", pane=0, urls=[uri("2.webm")], current=uri("2.webm"))
        client.tool("activate_items", pane=0, urls=[uri("2.webm")])
        viewer("the video plays, drawn", {"open": True, "loaded": True, "player.drawn": True, "player.muted": True,
                                          "player.paused": False})
        time.sleep(1.0)  # a few frames
        shots = pathlib.Path(args.screenshots) if args.screenshots else None
        image = screenshot(client, shots / "video.png" if shots else None)
        width, height = image.size
        middle = image.getpixel((width // 2, height // 2))
        if not (middle[0] > 180 and middle[1] < 70 and middle[2] < 70):
            raise Failure(f"the middle is {middle}, not the red of the video")
        print(f"ok    the video is drawn ({width}×{height}, middle {middle})")

        client.tool("viewer_input", action="key", key="Space")
        viewer("paused: the controls show", {"player.paused": True, "controlsVisible": True})
        time.sleep(0.3)
        image = screenshot(client, shots / "controls.png" if shots else None)
        # The bar lies over the bottom of the video: dark with white in it, not red.
        bottom = [image.getpixel((x, y)) for y in range(height - 60, height - 4, 2) for x in range(0, width, 4)]
        if not any(min(pixel) > 200 for pixel in bottom):
            raise Failure("no white of the controls at the bottom")
        print("ok    the controls are drawn over the video")

        client.tool("viewer_input", action="key", key="Up")
        viewer("paging to the image", {"name": "1.png", "loaded": True, "video": False})
        time.sleep(0.3)
        image = screenshot(client, shots / "image.png" if shots else None)
        middle = image.getpixel((width // 2, height // 2))
        if middle[0] > 180 and middle[1] < 70:
            raise Failure(f"the video still shows over the image: {middle}")
        print(f"ok    the image shows, not the video (middle {middle})")
        client.tool("viewer_input", action="key", key="Escape")
    finally:
        client.close()
        whale.terminate()
        try:
            whale.wait(timeout=10)
        except Exception:
            whale.kill()
        kwin.terminate()
        try:
            kwin.wait(timeout=10)
        except Exception:
            kwin.kill()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--whale", required=True)
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--plugin-dir", required=True)
    parser.add_argument("--screenshots", help="directory for the screenshots")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not shutil.which("kwin_wayland") or not shutil.which("dbus-run-session"):
        print("skipped: needs kwin_wayland and dbus-run-session")
        return SKIPPED
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("skipped: needs Pillow")
        return SKIPPED
    if not os.environ.get(PRIVATE_BUS):
        # A D-Bus session of its own: the virtual KWin must not meet the desktop's.
        env = dict(os.environ, **{PRIVATE_BUS: "1"})
        with tempfile.NamedTemporaryFile("w", suffix=".conf", prefix="whale-v3-bus-") as config:
            config.write(BUS_CONFIG)
            config.flush()
            return subprocess.call(["dbus-run-session", "--config-file", config.name, "--", sys.executable, *sys.argv],
                                   env=env)
    if args.screenshots:
        os.makedirs(args.screenshots, exist_ok=True)

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-v3-render-"))
    try:
        run(args, work)
    except Failure as failure:
        print(f"FAILED: {failure}")
        print(f"(logs: {work / 'profile' / 'whale.log'}, {work / 'kwin.log'})")
        args.keep = True
        return 1
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
    print("passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
