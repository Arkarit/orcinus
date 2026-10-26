#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of styles (ST3, .privatedoc/design-notes.md) with the user's two
styles, driven only through the dev MCP:

  A  the styles of orcinusstylesrc: actions with their shortcuts, View > Styles
  B  "Schlicht": one Compact view, nothing but the status bar; Styles in the background menu (F5)
  C  "Bilder": Icons with previews, the Places and Information panels, all bars
  D  saving "Bilder" again keeps its id, shortcut and an element of a newer Whale
  E  saving a new style "Zwei" with two views
  F  restart after "Schlicht": the window starts as it was left (E3), the styles are there
  G  back to "Bilder" after the restart
  H  the filter bars (FB3): media on the left, their texts on the right
  I  "Kissen" (LK7): the treemap on the right and the linked views

Whale runs on a private D-Bus session bus (the script starts one) and is restarted like O3.
Arguments as test_o3_acceptance.py.
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

# ViewProperties::toData() as KConfig writes it into orcinusstylesrc (newlines escaped).
COMPACT = r"[Dolphin]\nVersion=4\nViewMode=2\nPreviewsShown=false\n"
ICONS_WITH_PREVIEWS = r"[Dolphin]\nVersion=4\nViewMode=0\nPreviewsShown=true\n"
NO_PANELS = "places=false,folders=false,information=false,terminal=false"

STYLES = f"""[General]
Version=1
Order=schlicht,bilder

[Style][schlicht]
Name=Schlicht
Shortcut=Ctrl+Alt+1

[Style][schlicht][Element][split]
Enabled=true
Value=false

[Style][schlicht][Element][viewStyle.left]
Enabled=true
Value={COMPACT}

[Style][schlicht][Element][menuBar]
Enabled=true
Value=false

[Style][schlicht][Element][toolBars]
Enabled=true
Value=mainToolBar=false

[Style][schlicht][Element][fileViewToolBars]
Enabled=true
Value=false

[Style][schlicht][Element][statusBar]
Enabled=true
Value=FullWidth

[Style][schlicht][Element][panels]
Enabled=true
Value={NO_PANELS}

[Style][bilder]
Name=Bilder
Shortcut=Ctrl+Alt+2

[Style][bilder][Element][split]
Enabled=true
Value=false

[Style][bilder][Element][viewStyle.left]
Enabled=true
Value={ICONS_WITH_PREVIEWS}

[Style][bilder][Element][menuBar]
Enabled=true
Value=true

[Style][bilder][Element][toolBars]
Enabled=true
Value=mainToolBar=true

[Style][bilder][Element][fileViewToolBars]
Enabled=true
Value=true

[Style][bilder][Element][statusBar]
Enabled=true
Value=Small

[Style][bilder][Element][panels]
Enabled=true
Value=places=true,information=true

[Style][bilder][Element][fromTheFuture]
Enabled=true
Value=42
"""


class Session:
    """One Whale process with its MCP client, in the profile that survives restarts."""

    def __init__(self, args, work, folder):
        self.whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_st_acceptance", "version": "1"}})
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


SCHLICHT = {
    "paneCount": 1, "panes.0.viewMode": "compact", "panes.0.previews.shown": False,
    "styles.current.menuBar": "false", "styles.menuBarVisible": False,
    "styles.current.toolBars": {"$contains": "mainToolBar=false"},
    "toolbars.fileViewsVisible": False, "styles.current.statusBar": "FullWidth",
    "styles.current.panels": {"$contains": "information=false"},
}
BILDER = {
    "paneCount": 1, "panes.0.viewMode": "icons", "panes.0.previews.shown": True,
    "styles.current.menuBar": "true", "styles.menuBarVisible": True,
    "styles.current.toolBars": {"$contains": "mainToolBar=true"},
    "toolbars.fileViewsVisible": True, "styles.current.statusBar": "Small",
    "styles.current.panels": {"$contains": "information=true"},
}


def run(args, work):
    folder = work / "files"
    folder.mkdir()
    for name in ("a.jpg", "b.txt"):
        (folder / name).write_bytes(b"x")
    config = work / "profile" / "config"
    config.mkdir(parents=True)
    styles_file = config / "orcinusstylesrc"
    styles_file.write_text(STYLES)

    w = Session(args, work, folder)
    try:
        client = w.client
        s = Scenario(client, args.screenshots)
        s.require_keyboard_focus = False

        # A
        state = s.expect("A: the styles of the file", {
            "panes.0.loading": False,
            "styles.menu": ["whale_style_schlicht", "whale_style_bilder", "-", "whale_save_last_style", "whale_save_style"]}, focus=False)
        styles = {style["id"]: style for style in state["styles"]["list"]}
        check(styles["schlicht"]["actionShortcut"] == "Ctrl+Alt+1", "A: shortcut of Schlicht", styles["schlicht"])
        check(styles["bilder"]["actionShortcut"] == "Ctrl+Alt+2", "A: shortcut of Bilder", styles["bilder"])
        check(styles["bilder"]["elements"]["fromTheFuture"] == {"enabled": True, "known": False, "description": ""},
              "A: an element of a newer Whale", styles["bilder"]["elements"])
        check("Compact" in styles["schlicht"]["elements"]["viewStyle.left"]["description"], "A: description",
              styles["schlicht"]["elements"])
        print("ok    A: styles of the file")

        # B, C: back and forth
        for round in (1, 2):
            client.tool("apply_style", id="schlicht")
            s.expect(f"B{round}: Schlicht", SCHLICHT, focus=False)
            client.tool("apply_style", id="bilder")
            s.expect(f"C{round}: Bilder", BILDER, focus=False)
        check(client.tool_error("apply_style", id="nope") is not None, "C: an unknown style")
        print("ok    B, C: Schlicht and Bilder, back and forth")

        # B: the way back from a style without bars (F5): the menu of the view's background
        client.tool("apply_style", id="schlicht")
        s.expect("B: Schlicht again", SCHLICHT, focus=False)
        client.tool("open_context_menu", pane=0)
        s.expect("B: Styles in the background menu", {"popupMenu.names": {"$contains": "whale_styles"}}, focus=False)
        check("whale_show_file_view_toolbars" in client.state()["popupMenu"]["names"], "B: the File View Toolbars too")
        client.tool("close_popup")
        s.expect("B: menu closed", {"popupMenu": None}, focus=False)
        client.tool("apply_style", id="bilder")
        s.expect("C: Bilder again", BILDER, focus=False)
        client.tool("open_context_menu", pane=0)
        state = s.expect("C: background menu", {"popupMenu.names": {"$contains": "properties"}}, focus=False)
        check("whale_styles" not in state["popupMenu"]["names"], "C: no Styles while the bars are shown", state["popupMenu"])
        client.tool("close_popup")
        s.expect("C: menu closed", {"popupMenu": None}, focus=False)
        print("ok    B: the way back from Schlicht (F5)")

        # D: "Save Style" after applying Bilder, then saving "Bilder" again by name
        check(client.state()["styles"]["lastStyle"] == "bilder", "D: Bilder applied last")
        reply = client.tool("save_style")
        check('"id":"bilder"' in reply[0]["text"].replace(" ", ""), "D: Save Style saves Bilder", reply)
        reply = client.tool("save_style", name="bilder")
        check('"id":"bilder"' in reply[0]["text"].replace(" ", ""), "D: same id", reply)
        state = s.expect("D: still two styles", {
            "styles.menu": ["whale_style_schlicht", "whale_style_bilder", "-", "whale_save_last_style", "whale_save_style"]}, focus=False)
        bilder = state["styles"]["list"][1]
        check(bilder["name"] == "Bilder" and bilder["shortcut"] == "Ctrl+Alt+2", "D: name and shortcut kept", bilder)
        check("fromTheFuture" in bilder["elements"], "D: the element of a newer Whale kept", bilder)
        check("Value=42" in styles_file.read_text(), "D: written back to the file")
        print("ok    D: saved again")

        # E: a new style with two views
        client.tool("set_split_view", enabled=True)
        s.expect("E: two views", {"paneCount": 2, "splitAnimating": False}, focus=False)
        reply = client.tool("save_style", name="Zwei")
        check('"id":"zwei"' in reply[0]["text"].replace(" ", ""), "E: new id", reply)
        state = s.expect("E: three styles", {"styles.menu": {"$contains": "whale_style_zwei"}}, focus=False)
        zwei = state["styles"]["list"][2]
        check(zwei["elements"]["split"]["enabled"] and len(zwei["elements"]) == 14, "E: all elements captured", zwei)
        check("[Style][zwei]" in styles_file.read_text(), "E: in the file")
        client.tool("apply_style", id="bilder")
        s.expect("E: Bilder closes the right view", BILDER, focus=False)
        client.tool("apply_style", id="zwei")
        s.expect("E: Zwei opens it again", {"paneCount": 2, "styles.current.split": "true"}, focus=False)
        print("ok    E: new style")

        # F: restart after "Schlicht"
        client.tool("apply_style", id="schlicht")
        s.expect("F: Schlicht before the restart", SCHLICHT, focus=False)
        w.quit()
        w = Session(args, work, folder)
        client = w.client
        s = Scenario(client, args.screenshots)
        s.require_keyboard_focus = False
        s.expect("F: restarted as Schlicht", dict(SCHLICHT, **{
            "panes.0.loading": False,
            "styles.menu": ["whale_style_schlicht", "whale_style_bilder", "whale_style_zwei", "-", "whale_save_last_style", "whale_save_style"]}),
            focus=False)
        print("ok    F: restart")

        # G
        client.tool("apply_style", id="bilder")
        s.expect("G: Bilder after the restart", BILDER, focus=False)
        print("ok    G: Bilder after the restart")

        # H: a style with a filter on each side (FB3)
        client.tool("set_split_view", enabled=True)
        s.expect("H: two views", {"paneCount": 2, "splitAnimating": False}, focus=False)
        client.tool("set_filter_text", pane=0, text="*.jpg")
        client.tool("set_filter_text", pane=1, text="*.txt")
        s.expect("H: filtered", {"panes.0.items": ["a.jpg"], "panes.1.items": ["b.txt"]}, focus=False)
        reply = client.tool("save_style", name="Medien")
        check('"id":"medien"' in reply[0]["text"].replace(" ", ""), "H: saved", reply)
        client.tool("clear_filter", pane=0)
        client.tool("clear_filter", pane=1)
        client.tool("apply_style", id="bilder")
        s.expect("H: Bilder, one view", {"paneCount": 1, "panes.0.filter.text": ""}, focus=False)
        client.tool("apply_style", id="medien")
        state = s.expect("H: media left, texts right", {
            "paneCount": 2, "panes.0.items": ["a.jpg"], "panes.1.items": ["b.txt"],
            "panes.0.filter.barVisible": True, "panes.0.filter.scope": "files"}, focus=False)
        # The element keys contain a dot, which the state paths of expect() cannot.
        check(state["styles"]["current"]["filterBar.right"].endswith("text=*.txt"), "H: captured right filter",
              state["styles"]["current"])
        medien = [style for style in state["styles"]["list"] if style["id"] == "medien"][0]
        check("*.jpg" in medien["elements"]["filterBar.left"]["description"], "H: description", medien["elements"])
        print("ok    H: filter bars")

        # I: the shown view and the lock (LK7, E8/E9)
        client.tool("clear_filter", pane=0)
        client.tool("clear_filter", pane=1)
        client.tool("set_view", pane=1, view="orcinus_treemap")
        s.expect("I: treemap on the right", {"panes.1.viewId": "orcinus_treemap", "panes.1.treemap.status": "ready"}, focus=False)
        client.tool("activate_pane", pane="left")
        client.tool("set_split_lock", locked=True)
        s.expect("I: linked", {"splitLock.locked": True}, focus=False)
        reply = client.tool("save_style", name="Kissen")
        check('"id":"kissen"' in reply[0]["text"].replace(" ", ""), "I: saved", reply)
        client.tool("set_split_lock", locked=False)
        client.tool("set_view", pane=1, view="dolphin")
        client.tool("apply_style", id="bilder")
        s.expect("I: Bilder, one view", {"paneCount": 1, "splitLock.locked": False}, focus=False)
        client.tool("apply_style", id="kissen")
        state = s.expect("I: Kissen again: treemap on the right, linked", {
            "paneCount": 2, "panes.1.viewId": "orcinus_treemap", "splitLock.locked": True,
            "panes.0.viewId": "dolphin"}, focus=False)
        kissen = [style for style in state["styles"]["list"] if style["id"] == "kissen"][0]
        check(kissen["elements"]["view.right"]["description"] == "Cushion Treemap", "I: description", kissen["elements"])
        check(kissen["elements"]["splitLock"]["description"] == "Linked", "I: lock description", kissen["elements"])
        print("ok    I: treemap and lock")
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
    if os.environ.get("WHALE_ST_PRIVATE_BUS") != "1":
        # Run again on a private session bus of our own (see test_m6_acceptance.py).
        env = dict(os.environ, WHALE_ST_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-st-"))
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
    print("ST acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
