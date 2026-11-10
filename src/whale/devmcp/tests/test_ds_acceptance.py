#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the default setup of a new user (DS), driven only through the dev MCP:

  A first start in an empty profile: the shipped profiles, "Compact" applied, the default
    toolbars
  B a restart: the profiles are not created or applied again
  C all profiles removed by the user: still none after a restart

Whale runs on a private D-Bus session bus (the script starts one) and is restarted like O3.
Arguments as test_o3_acceptance.py.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance
from test_tb_acceptance import FILE_VIEW_DEFAULT, MAIN_DEFAULT, check

SHIPPED = ["compact", "dual-compact", "details", "dual-details", "images-only", "locked-cushion-map"]
MENU_TAIL = ["-", "whale_save_last_style", "whale_save_style"]


class Session:
    """One Whale process with its MCP client, in the profile that survives restarts."""

    def __init__(self, args, work, folder):
        self.whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True,
                                 first_start=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_ds_acceptance", "version": "1"}})
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


def scenario(session):
    s = Scenario(session.client, None)
    s.require_keyboard_focus = False
    return s


def run(args, work):
    folder = work / "files"
    folder.mkdir()
    (folder / "a.txt").write_bytes(b"x")
    (folder / "b.png").write_bytes(b"x")
    styles_file = work / "profile" / "config" / "orcinusstylesrc"

    # A
    w = Session(args, work, folder)
    try:
        s = scenario(w)
        state = s.expect("A: first start", {
            "panes.0.loading": False, "paneCount": 1,
            "styles.lastStyle": "compact",
            "styles.menu": ["whale_style_" + id for id in SHIPPED] + MENU_TAIL,
            "styles.current.split": "false", "styles.current.statusBar": "FullWidth",
            "styles.current.panels": "places=true,folders=false,information=false,terminal=false",
            "styles.menuBarVisible": False,
            "toolbars.global.layout": MAIN_DEFAULT}, focus=False)
        # The location bar is in the row of its toolbar where there is room (offscreen windows start small).
        w.client.tool("set_window_size", width=1200, height=700)
        s.expect("A: File View Toolbar", {"window.width": 1200, "panes.0.toolbar.layout": FILE_VIEW_DEFAULT,
                                          "panes.0.toolbar.locationBar.wrapped": False}, focus=False)
        current = state["styles"]["current"]
        check("ViewMode=2" in current["viewStyle.left"], "A: Compact view", current["viewStyle.left"])
        check(current["filterBar.left"].startswith("visible=false,"), "A: no filter bar", current["filterBar.left"])
        check(styles_file.exists(), "A: the profiles are saved")
        print("ok    A: first start")

        # B: another profile applied last
        session_client = w.client
        session_client.tool("apply_style", id="details")
        s.expect("B: Details applied", {"styles.lastStyle": "details"}, focus=False)
        w.quit()
    except BaseException:
        w.kill()
        raise

    w = Session(args, work, folder)
    try:
        s = scenario(w)
        s.expect("B: restarted", {
            "panes.0.loading": False, "styles.lastStyle": "details",
            "styles.menu": ["whale_style_" + id for id in SHIPPED] + MENU_TAIL}, focus=False)
        print("ok    B: restart, nothing created or applied again")
        w.quit()
    except BaseException:
        w.kill()
        raise

    # C
    styles_file.write_text("[General]\nVersion=1\n")
    w = Session(args, work, folder)
    try:
        s = scenario(w)
        s.expect("C: no profiles", {"panes.0.loading": False, "styles.menu": MENU_TAIL[1:]}, focus=False)
        print("ok    C: removed profiles stay removed")
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
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_DS_PRIVATE_BUS") != "1":
        # Run again on a private session bus of our own (see test_m6_acceptance.py).
        env = dict(os.environ, WHALE_DS_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
        with tempfile.TemporaryFile() as output:
            code = subprocess.call(["dbus-run-session", "--", sys.executable, *sys.argv], env=env, stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT)
            output.seek(0)
            for line in output.read().decode(errors="replace").splitlines():
                if not line.startswith(("dbus-daemon[", "kf.wallet", "qt.qpa.services", "A connection to the bus")):
                    print(line)
        return code

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-ds-"))
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
    print("DS acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
