#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the own style of a view (VS, .privatedoc/design-notes.md),
driven only through the dev MCP: the window a view pops out to ("Pop out") starts with the
style of that view, given on the command line, and with a single view.

  A  the style of the command line, a single view although new windows split
  B  the view keeps that style in another folder
  C  nothing is written down for the position until the style is changed

Arguments as test_m4_acceptance.py.
"""

import argparse
import base64
import os
import pathlib
import shutil
import sys
import tempfile
import time

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance

# The text DolphinView::ownStyleData() hands over (ViewProperties::toData()): Compact, hidden
# files shown.
STYLE = b"[Dolphin]\nVersion=4\nViewMode=2\n\n[Settings]\nHiddenFilesShown=true\n"


def stored_style(profile):
    """What is written down for the left position, or "" (extended attribute or .directory)."""
    folder = profile / "data" / "orcinus" / "view_properties" / "global"
    if not folder.exists():
        return ""
    try:
        return os.getxattr(folder, "user.kde.fm.viewproperties#1").decode()
    except OSError:
        directory = folder / ".directory"
        return directory.read_text() if directory.exists() else ""


def run(args, work):
    folder = work / "files"
    (folder / "sub").mkdir(parents=True)
    (folder / "a.txt").write_bytes(b"a")
    (folder / ".hidden").write_bytes(b"h")
    profile = work / "profile"
    config = profile / "config"
    config.mkdir(parents=True)
    # New windows split: the popped out view comes on its own anyway.
    (config / "orcinusrc").write_text("[General]\nSplitViewAtStartup=AlwaysSplit\n")

    style = base64.urlsafe_b64encode(STYLE).decode().rstrip("=")
    whale = start_whale(args.whale, folder, profile, args.plugin_dir, args.platform, automation=True,
                        extra_args=("--view-style", style))
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_vs_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)

        s.expect("A: the style of the popped out view, on its own", {
            "paneCount": 1, "panes.0.loading": False, "panes.0.viewMode": "compact",
            "panes.0.ownViewMode": "compact", "panes.0.hiddenFiles": True})
        print("ok    A: style from the command line")

        client.tool("set_location", pane=0, url=(folder / "sub").as_uri())
        s.expect("B: kept in another folder", {"panes.0.url": (folder / "sub").as_uri(), "panes.0.loading": False,
                                               "panes.0.viewMode": "compact"})
        print("ok    B: kept")

        if "ViewMode=2" in stored_style(profile):
            raise Failure("C: the style taken was written down: " + stored_style(profile))
        client.tool("trigger_action", action="view_details")
        s.expect("C: changed", {"panes.0.viewMode": "details", "panes.0.ownViewMode": "details"})
        deadline = time.monotonic() + 10
        while "ViewMode=1" not in stored_style(profile):
            if time.monotonic() > deadline:
                raise Failure("C: the changed style was not written down: " + stored_style(profile))
            time.sleep(0.1)
        print("ok    C: written down when changed")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-vs-"))
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
    print("VS acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
