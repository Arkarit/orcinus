#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the view plugin milestone M5 (action gating, rename, file
actions, context menu, item information), driven only through the dev MCP.

  A gating   B rename   C copy/cut/paste   D trash/delete   E context menu
  F item information   G split view

Whale runs in a throwaway profile with WHALE_DEV_MCP_FILE_OPERATIONS=1: its trash is
the profile's, deleting needs no confirmation, and all files live in a temporary
folder. Copy/cut/paste use the clipboard; outside of the offscreen platform that is
the user's own, so C only runs there unless --clipboard is given. E (context menus)
only runs offscreen: Wayland compositors show popups only in answer to real input.
Arguments as test_m3_acceptance.py.
"""

import argparse
import os
import pathlib
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance


def files(folder):
    return sorted(path.name for path in folder.iterdir() if path.is_file())


def run(args, work):
    folder = work / "files"
    (folder / "sub").mkdir(parents=True)
    (folder / "other").mkdir()
    for name in ("a.txt", "b.txt", "c.txt"):
        (folder / name).write_bytes(name.encode() * 10)
    for name in ("x.txt", "y.txt"):
        (folder / "other" / name).write_bytes(b"o")
    trash = work / "profile" / "data" / "Trash" / "files"

    def url(*parts):
        return folder.joinpath(*parts).as_uri()

    whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True,
                        opener_log=work / "opened.log", file_operations=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_m5_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)
        client.tool("set_hidden_files", shown=False)
        client.tool("set_sort", field="name", order="ascending", folders_first=True)
        s.expect("folder loaded", {"panes.0.loading": False, "panes.0.items": ["other", "sub", "a.txt", "b.txt", "c.txt"]})

        # A: gating per view
        s.expect("A: file list, nothing gated", {
            "actions.view_zoom_in.gate": None, "actions.view_zoom_in.enabled": True,
            "actions.toggle_selection_mode.gate": None, "actions.movetotrash.gate": None})
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("A: test list gates what it cannot honor", {
            "actions.view_zoom_in.gate": "no Zoom capability", "actions.view_zoom_in.enabled": False,
            "actions.show_preview.gate": None,
            "actions.toggle_selection_mode.enabled": False,
            "actions.sort.gate": None, "actions.edit_select_all.gate": None,
            "actions.movetotrash.gate": "nothing selected", "actions.movetotrash.enabled": False,
            "actions.renamefile.enabled": False, "actions.edit_copy.enabled": False})
        client.tool("set_view", pane=0, view="whale_hello")
        s.expect("A: a view without capabilities", {
            "actions.sort.gate": "no HostOrder capability", "actions.sort.enabled": False,
            "actions.show_preview.gate": "no Previews capability",
            "actions.edit_select_all.gate": "no Selection capability",
            "actions.movetotrash.gate": "no Selection capability", "actions.properties.enabled": True})
        client.tool("set_view", pane=0, view="whale_testlist")
        client.tool("set_selection", pane=0, urls=[url("a.txt")], current=url("a.txt"))
        s.expect("A: a selection opens the gates of the file actions", {
            "actions.movetotrash.gate": None, "actions.movetotrash.enabled": True,
            "actions.renamefile.enabled": True, "actions.edit_copy.enabled": True,
            "actions.view_zoom_in.enabled": False})

        # B: rename with the dialog (the test list has no InlineRename)
        client.tool("trigger_action", action="rename")
        s.expect("B: rename dialog, no inline editor", {"dialogs.0.type": "rename", "panes.0.selectionMode": False},
                 focus=False)
        client.tool("rename_dialog", new_name="renamed.txt")
        if args.platform == "offscreen":
            s.require_keyboard_focus = False
        s.expect("B: renamed and still selected", {
            "dialogs": [], "panes.0.selectedNames": ["renamed.txt"], "panes.0.pluginSelection": ["renamed.txt"],
            "panes.0.currentName": "renamed.txt", "focus.role": "plugin:whale_testlist"})
        if files(folder) != ["b.txt", "c.txt", "renamed.txt"]:
            raise Failure(f"after renaming: {files(folder)}")

        # C: copy/cut/paste on the test list's selection
        if args.platform == "offscreen" or args.clipboard:
            client.tool("set_selection", pane=0, urls=[url("b.txt")], current=url("b.txt"))
            s.expect("C: b.txt selected", {"actions.edit_copy.enabled": True})
            client.tool("trigger_action", action="copy")
            s.expect("C: clipboard holds b.txt", {"clipboard": {"urls": [url("b.txt")], "cut": False}})
            client.tool("set_location", pane=0, url=url("sub"))
            s.expect("C: in sub", {"url": url("sub"), "panes.0.loading": False, "actions.edit_paste.enabled": True})
            client.tool("trigger_action", action="paste")
            s.expect("C: pasted", {"panes.0.pluginRows": ["b.txt"]})
            client.tool("trigger_action", action="go_back")
            s.expect("C: back", {"panes.0.loading": False, "panes.0.pluginRows": ["other", "sub", "b.txt", "c.txt", "renamed.txt"]})
            client.tool("set_selection", pane=0, urls=[url("c.txt")], current=url("c.txt"))
            s.expect("C: c.txt selected", {"actions.edit_cut.enabled": True})
            client.tool("trigger_action", action="cut")
            s.expect("C: clipboard holds c.txt to move", {"clipboard": {"urls": [url("c.txt")], "cut": True}})
            client.tool("set_location", pane=0, url=url("sub"))
            s.expect("C: in sub again", {"url": url("sub"), "panes.0.loading": False})
            client.tool("trigger_action", action="paste")
            s.expect("C: moved", {"panes.0.pluginRows": ["b.txt", "c.txt"]})
            if files(folder / "sub") != ["b.txt", "c.txt"] or "c.txt" in files(folder):
                raise Failure(f"after cut/paste: {files(folder)} / {files(folder / 'sub')}")
            client.tool("trigger_action", action="go_back")
            s.expect("C: back home", {"panes.0.loading": False, "panes.0.pluginRows": ["other", "sub", "b.txt", "renamed.txt"]})
        else:
            print("skip  C: the clipboard is the user's own here (use --clipboard)")
            (folder / "c.txt").rename(folder / "sub" / "c.txt")  # same state as after C
            s.expect("C (skipped): same state", {"panes.0.pluginRows": ["other", "sub", "b.txt", "renamed.txt"]})

        # D: trash and delete exactly the selection; nothing without a selection
        client.tool("set_selection", pane=0, urls=[url("renamed.txt")], current=url("renamed.txt"))
        s.expect("D: renamed.txt selected", {"actions.movetotrash.enabled": True})
        client.tool("trigger_action", action="trash")
        s.expect("D: trashed", {"panes.0.pluginRows": ["other", "sub", "b.txt"]})
        if not (trash / "renamed.txt").exists() or files(folder) != ["b.txt"]:
            raise Failure(f"after trash: {files(folder)}, trash {sorted(p.name for p in trash.iterdir())}")
        client.tool("set_selection", pane=0, urls=[])
        s.expect("D: nothing selected, trash and delete gated", {
            "panes.0.selectedNames": [], "actions.movetotrash.enabled": False, "actions.deletefile.enabled": False,
            "actions.movetotrash.gate": "nothing selected"})
        if client.tool_error("trigger_action", action="trash") is None:
            raise Failure("trash without a selection was accepted")
        s.expect("D: no selection mode, nothing changed", {"panes.0.selectionMode": False, "panes.0.pluginRows": ["other", "sub", "b.txt"]})
        client.tool("set_selection", pane=0, urls=[url("b.txt")], current=url("b.txt"))
        s.expect("D: b.txt selected", {"actions.deletefile.enabled": True})
        client.tool("trigger_action", action="delete")
        s.expect("D: deleted", {"panes.0.pluginRows": ["other", "sub"]})
        if files(folder) or (trash / "b.txt").exists():
            raise Failure(f"after delete: {files(folder)}")

        # E: context menus. On Wayland the compositor only allows a popup in answer to real
        # input ("Failed to create grabbing popup"), which an automation cannot provide.
        for name in ("d.txt", "e.txt", "f.txt"):
            (folder / name).write_bytes(b"e")
        s.expect("E: new files listed", {"panes.0.pluginRows": ["other", "sub", "d.txt", "e.txt", "f.txt"]})
        if args.platform == "offscreen":
            client.tool("set_selection", pane=0, urls=[url("d.txt"), url("e.txt")], current=url("e.txt"))
            client.tool("open_context_menu", pane=0, url=url("e.txt"))
            s.expect("E: menu for the two selected items", {
                "popupMenu.names": {"$contains": "properties"}, "panes.0.selectedNames": ["d.txt", "e.txt"]}, focus=False)
            menu = client.state()["popupMenu"]
            for wanted in ("renamefile", "edit_copy", "edit_cut"):
                if wanted not in menu["names"]:
                    raise Failure(f"menu without {wanted}: {menu['names']}")
            client.tool("close_popup")
            s.expect("E: menu closed", {"popupMenu": None})
            client.tool("open_context_menu", pane=0, url=url("f.txt"))
            s.expect("E: right click on an unselected item selects it", {
                "popupMenu.names": {"$contains": "renamefile"}, "panes.0.selectedNames": ["f.txt"],
                "panes.0.pluginSelection": ["f.txt"]}, focus=False)
            client.tool("close_popup")
            client.tool("set_selection", pane=0, urls=[])
            client.tool("open_context_menu", pane=0)
            s.expect("E: menu of the folder", {"popupMenu.names": {"$contains": "properties"}, "panes.0.selectedNames": []},
                     focus=False)
            if "renamefile" in client.state()["popupMenu"]["names"]:
                raise Failure("the folder's menu offers renaming")
            client.tool("close_popup")
            s.expect("E: closed", {"popupMenu": None})
        else:
            print("skip  E: popups need real input on this platform (covered offscreen and by whaleactionstest)")

        # F: item information
        client.tool("request_item_info", pane=0, url=url("e.txt"))
        s.expect("F: information panel gets e.txt", {"shell.itemInfo": "e.txt"})
        client.tool("request_item_info", pane=0)
        s.expect("F: and nothing afterwards", {"shell.itemInfo": None})

        # G: split view; each action works on the active half only
        client.tool("set_split_view", enabled=True)
        s.expect("G: split", {"splitAnimating": False, "paneCount": 2, "activePane": 1, "panes.1.loading": False})
        client.tool("set_location", pane=1, url=url("other"))
        s.expect("G: right in other", {"panes.1.url": url("other"), "panes.1.loading": False})
        s.expect("G: right is the file list: its own rules", {
            "panes.1.viewId": "dolphin", "actions.movetotrash.gate": None, "actions.toggle_selection_mode.gate": None})
        client.tool("set_view", pane=1, view="whale_testlist")
        client.tool("set_selection", pane=1, urls=[url("other", "x.txt")], current=url("other", "x.txt"))
        client.tool("set_selection", pane=0, urls=[url("d.txt")], current=url("d.txt"))
        client.tool("activate_pane", pane="left")
        s.expect("G: left active", {"activePane": 0, "actions.movetotrash.enabled": True, "panes.0.selectedNames": ["d.txt"]})
        client.tool("activate_pane", pane="right")
        s.expect("G: right active", {"activePane": 1, "actions.movetotrash.enabled": True, "panes.1.selectedNames": ["x.txt"]})
        client.tool("trigger_action", action="trash")
        s.expect("G: only the right selection trashed", {"panes.1.pluginRows": ["y.txt"], "panes.0.selectedNames": ["d.txt"]})
        if (folder / "other" / "x.txt").exists() or not (folder / "d.txt").exists():
            raise Failure("trash hit the wrong half")
        client.tool("set_selection", pane=1, urls=[])
        s.expect("G: right without a selection gates at once", {"actions.movetotrash.gate": "nothing selected"})
        client.tool("activate_pane", pane="left")
        s.expect("G: left still has its selection", {"activePane": 0, "actions.movetotrash.gate": None,
                                                      "panes.0.selectedNames": ["d.txt"]})
        client.tool("set_split_view", enabled=False)
        s.expect("G: split closed", {"paneCount": 1})
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
    parser.add_argument("--clipboard", action="store_true", help="run C also on a real platform (uses its clipboard)")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        print("skipped: no D-Bus session bus")
        return 77
    if args.screenshots:
        os.makedirs(args.screenshots, exist_ok=True)

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-m5-"))
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
    print("M5 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
