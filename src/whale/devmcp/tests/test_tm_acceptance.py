#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the menu bar as an element of the main toolbar (TM3), driven only
through the dev MCP:

  A by default: no element, the hamburger menu's button as before
  B Configure Toolbars adds the element: all menus, the hamburger button without width (F2)
  C a narrow window: a single menu button, wide again: the menus (E3)
  D independent of "Show Menubar" (E1), switched by styles
  E restart: the element stays
  F Configure Toolbars removes it again

The dialog is operated through its widgets (configure_toolbars). Whale runs on a private
D-Bus session bus (the script starts one) and is restarted like O3. Arguments as
test_o3_acceptance.py.
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

MENUS = ["File", "Edit", "View", "Go", "Tools", "Settings", "Help"]

# Two styles that only show or hide the classic menu bar (D).
STYLES = """[General]
Version=1
Order=mit,ohne

[Style][mit]
Name=Mit

[Style][mit][Element][menuBar]
Enabled=true
Value=true

[Style][ohne]
Name=Ohne

[Style][ohne][Element][menuBar]
Enabled=true
Value=false
"""


class Session:
    """One Whale process with its MCP client, in the profile that survives restarts."""

    def __init__(self, args, work, folder):
        self.whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_tm_acceptance", "version": "1"}})
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


def check(condition, message, detail=None):
    if not condition:
        raise Failure(message + ("" if detail is None else f": {json.dumps(detail)[:2000]}"))


def configure(client, **request):
    return json.loads(client.tool("configure_toolbars", **request)[0]["text"])["dialog"]


def new_session(args, work, folder):
    w = Session(args, work, folder)
    s = Scenario(w.client, args.screenshots)
    s.require_keyboard_focus = False
    return w, s


def run(args, work):
    folder = work / "files"
    folder.mkdir()
    (folder / "a.txt").write_bytes(b"x")
    config = work / "profile" / "config"
    config.mkdir(parents=True)
    (config / "whalestylesrc").write_text(STYLES)

    w, s = new_session(args, work, folder)
    try:
        client = w.client
        client.tool("set_window_size", width=1200, height=700)

        # A
        s.expect("A: no element by default", {
            "panes.0.loading": False, "window.width": 1200, "styles.menuBarVisible": False,
            "toolbars.global.menuBar": {"present": False},
            "toolbars.global.layout": {"$contains": "hamburger_menu"}}, focus=False)
        print("ok    A: by default")

        # B
        configure(client, open=True, select="global", insert="whale_menu_bar", ok=True)
        state = s.expect("B: the element in the main toolbar", {
            "toolbarDialog.open": False,
            "toolbars.global.layout": {"$contains": "whale_menu_bar"},
            "toolbars.global.menuBar.present": True, "toolbars.global.menuBar.mode": "menus",
            "toolbars.global.menuBar.shown": True, "toolbars.global.menuBar.menus": MENUS,
            "toolbars.global.menuBar.hamburgerWidth": 0}, focus=False)
        element = state["toolbars"]["global"]["menuBar"]
        check(element["width"] == element["fullWidth"], "B: as wide as all menus", element)
        print("ok    B: added with Configure Toolbars")

        # C
        full_width = element["fullWidth"]
        client.tool("set_window_size", width=full_width + 60, height=700)
        state = s.expect("C: narrow: a menu button", {
            "window.width": full_width + 60, "toolbars.global.menuBar.mode": "burger",
            "toolbars.global.menuBar.shown": True, "toolbars.global.menuBar.hamburgerWidth": 0}, focus=False)
        check(state["toolbars"]["global"]["menuBar"]["width"] < full_width / 2, "C: small",
              state["toolbars"]["global"]["menuBar"])
        client.tool("set_window_size", width=1200, height=700)
        s.expect("C: wide again: the menus", {
            "window.width": 1200, "toolbars.global.menuBar.mode": "menus",
            "toolbars.global.menuBar.width": full_width}, focus=False)
        print("ok    C: menus or a menu button")

        # D
        client.tool("apply_style", id="mit")
        s.expect("D: the classic menu bar too", {
            "styles.menuBarVisible": True, "toolbars.global.menuBar.mode": "menus",
            "toolbars.global.menuBar.shown": True}, focus=False)
        client.tool("apply_style", id="ohne")
        s.expect("D: only the element", {
            "styles.menuBarVisible": False, "toolbars.global.menuBar.shown": True,
            # KHamburgerMenu shows its button again with the classic menu bar hidden: still no room (F2).
            "toolbars.global.menuBar.hamburgerWidth": 0}, focus=False)
        print("ok    D: independent of Show Menubar")

        # E
        w.quit()
        w, s = new_session(args, work, folder)
        client = w.client
        client.tool("set_window_size", width=1200, height=700)
        s.expect("E: restarted with the element", {
            "panes.0.loading": False, "window.width": 1200,
            "toolbars.global.menuBar.present": True, "toolbars.global.menuBar.mode": "menus",
            "toolbars.global.menuBar.menus": MENUS, "toolbars.global.menuBar.hamburgerWidth": 0}, focus=False)
        print("ok    E: restart")

        # F
        configure(client, open=True, select="global", remove="whale_menu_bar", ok=True)
        state = s.expect("F: removed again", {
            "toolbarDialog.open": False, "toolbars.global.menuBar": {"present": False}}, focus=False)
        check("whale_menu_bar" not in state["toolbars"]["global"]["layout"], "F: not in the layout",
              state["toolbars"]["global"]["layout"])
        print("ok    F: removed with Configure Toolbars")
        w.quit()
    finally:
        w.kill()


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
    if os.environ.get("WHALE_TM_PRIVATE_BUS") != "1":
        # Run again on a private session bus of our own (see test_m6_acceptance.py).
        env = dict(os.environ, WHALE_TM_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-tm-"))
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
    print("TM acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
