#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of T1 (operations on deep entries of a recursive view, here the
treemap orcinus_treemap), driven only through the dev MCP.

  A open a deep file           B show in folder           C properties
  D rename                     E trash                    F delete (confirmation)
  G Whale's actions act on the deep entry (T2)
  H split view: an operation of the left treemap leaves the right half alone
  I reload: the treemap scans again after an operation; F5; the cache is not stale
  K copy and cut (offscreen only: elsewhere the clipboard is the user's)

The test tree is made here in a temporary folder (no user data). Whale runs on a private
D-Bus session bus with file operations allowed, a home and trash of its own, KDE's default
confirmations (deleting asks, trashing does not) and a test opener for text files.

Context menus are opened with real input (a right click, the Menu key) and their entries
chosen with choose_popup_entry. Popup menus need real input on Wayland (a synthesized
click has no input serial), so with --popups off (the default outside of offscreen) the
same steps check the menu the treemap would show (treemap.selectedMenu) and carry out the
operation through file_operation, the host API the menu entries call.
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
import urllib.parse

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance
import wayland_real_input

FILE_MENU = ["open", "open_with", "show_in_folder", "copy", "cut", "copy_location", "rename", "trash", "delete", "properties"]
FOLDER_MENU = ["open", "show_in_folder", "copy", "cut", "copy_location", "rename", "trash", "delete", "properties"]
ROUTED = ["movetotrash", "deletefile", "renamefile", "edit_cut", "edit_copy", "copy_location"]

# The text of each menu entry (offscreen, untranslated).
ENTRY_TEXT = {"open": "Open", "open_with": "Open With…", "show_in_folder": "Show in Folder",
              "show_in_parent_folder": "Show in Parent Folder", "copy": "Copy", "cut": "Cut",
              "copy_location": "Copy Location", "rename": "Rename…", "trash": "Move to Trash", "delete": "Delete",
              "properties": "Properties"}
OPERATION_OF = {"open": "open", "open_with": "open_with", "show_in_folder": "show_in_folder",
                "show_in_parent_folder": "show_in_folder", "copy": "copy", "cut": "cut",
                "copy_location": "copy_location", "rename": "rename", "trash": "trash", "delete": "delete",
                "properties": "properties"}


def make_file(path, size):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"w" * size)


def make_tree(root):
    """The tree of the T1 plan, plus a folder and a file for the split view."""
    make_file(root / "a/deep.txt", 40000)
    make_file(root / "a/rename-me.txt", 30000)
    make_file(root / "a/trash-me.txt", 30000)
    make_file(root / "a/delete-me.txt", 30000)
    make_file(root / "a/split-me.txt", 30000)
    make_file(root / "a/sub/inner.txt", 30000)
    make_file(root / "b/target.txt", 50000)
    make_file(root / "top.txt", 20000)


def uri(path):
    """A URL as Whale's state shows it (QUrl::toString(): spaces are not encoded)."""
    return urllib.parse.unquote(pathlib.Path(path).as_uri())


def tm(pane=0):
    return f"panes.{pane}.treemap"


def wait_until(title, check, timeout=10):
    deadline = time.monotonic() + timeout
    while not check():
        if time.monotonic() > deadline:
            raise Failure(f"{title}: timed out")
        time.sleep(0.05)
    print(f"ok    {title}")


def stable_canvas(client, pane=0):
    def size():
        treemap = client.state()["panes"][pane]["treemap"]
        return treemap["canvasWidth"], treemap["canvasHeight"]
    first = size()
    time.sleep(0.4)
    return size() == first


class Menu:
    """Opens the treemap's context menu of an entry and chooses an entry: with synthesized
    input through the window (popups on, offscreen), with real input of the session (real,
    Wayland: see wayland_real_input.py), or through the host API that the entries call
    (popups off)."""

    def __init__(self, client, scenario, popups, real=None):
        self.client = client
        self.s = scenario
        self.popups = popups
        self.real = real

    def open(self, url, expected, folder=False, pane=0):
        """Shows the menu of @p url (selected first); checks that its entries are @p expected."""
        if self.real:
            if folder:
                self.client.tool("treemap_select", pane=pane, url=url)
                self.s.expect(f"{pathlib.PurePath(url).name} selected", {f"{tm(pane)}.selected": url})
                self.real.key(wayland_real_input.KEY_COMPOSE)
            else:
                # Where the cell is (a synthesized hover tells; layouts animated by the split
                # view must have settled), then off it again, so only the real pointer can make
                # it the hovered cell.
                wait_until("layout settled", lambda: stable_canvas(self.client, pane))
                where = json.loads(self.client.tool("treemap_hover", pane=pane, url=url)[0]["text"])
                self.client.tool("treemap_hover", pane=pane)
                self.real.move_to_window_position(where["windowX"], where["windowY"])
                # Never click before Whale confirms that the real pointer is on the cell.
                self.s.expect(f"real pointer on {pathlib.PurePath(url).name}", {f"{tm(pane)}.hovered": url}, timeout_ms=3000)
                self.real.right_click()
            texts = [ENTRY_TEXT["show_in_parent_folder" if folder and name == "show_in_folder" else name] for name in expected]
            # Exactly one menu: Qt sends a real Menu key as a context menu event and a key press.
            self.s.expect(f"real menu of {pathlib.PurePath(url).name}", {
                f"{tm(pane)}.selected": url, "popupMenu.names": expected, "popupMenu.texts": texts,
                f"{tm(pane)}.openMenus": 1}, focus=False)
        elif self.popups:
            if folder:
                # A folder is covered by its entries: select it, then the Menu key.
                self.client.tool("treemap_select", pane=pane, url=url)
                self.client.tool("treemap_key", pane=pane, key="Menu")
            else:
                self.client.tool("treemap_context_menu", pane=pane, url=url)
            texts = [ENTRY_TEXT["show_in_parent_folder" if folder and name == "show_in_folder" else name] for name in expected]
            self.s.expect(f"menu of {pathlib.PurePath(url).name}", {
                f"{tm(pane)}.selected": url, "popupMenu.names": expected, "popupMenu.texts": texts,
                f"{tm(pane)}.openMenus": 1}, focus=False)
        else:
            self.client.tool("treemap_select", pane=pane, url=url)
            self.s.expect(f"menu of {pathlib.PurePath(url).name} (as the treemap would show it)", {
                f"{tm(pane)}.selected": url, f"{tm(pane)}.selectedMenu": expected})
        self.url = url
        self.folder = folder
        self.pane = pane

    def choose(self, name):
        if self.real:
            # Real keys: Down until the entry is the active one, then Enter.
            for _ in range(12):
                if self.client.state()["popupMenu"]["active"] == name:
                    break
                self.real.key(wayland_real_input.KEY_DOWN)
                time.sleep(0.05)
            self.s.expect(f"entry {name} active", {"popupMenu.active": name}, focus=False)
            self.real.key(wayland_real_input.KEY_ENTER)
        elif self.popups:
            self.client.tool("choose_popup_entry",
                             text=ENTRY_TEXT["show_in_parent_folder" if self.folder and name == "show_in_folder" else name])
        else:
            self.client.tool("file_operation", pane=self.pane, operation=OPERATION_OF[name], urls=[self.url])


class Session:
    def __init__(self, args, work, folder, opener_log):
        self.whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True,
                                 opener_log=opener_log, file_operations=True, confirm_delete=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_t1_acceptance", "version": "1"}})
        self.client.notify("notifications/initialized")
        wait_for_instance(self.client, self.whale)

    def kill(self):
        self.client.close()
        if self.whale.poll() is None:
            self.whale.terminate()
            self.whale.wait(timeout=10)


def run(args, work):
    root = work / "files" / "root"
    make_tree(root)
    opener_log = work / "opened.log"
    trash = work / "profile" / "data" / "Trash" / "files"
    url = uri(root)
    u = lambda path: uri(root / path)
    real_input = args.popups == "real"
    popups = real_input or args.popups == "on" or (args.popups == "auto" and args.platform == "offscreen")
    print("popup menus: " + ("real input of the session (ydotool)" if real_input else "synthesized input" if popups
                             else "checked through treemap.selectedMenu, operations through file_operation"))

    w = Session(args, work, root, opener_log)
    real = None
    try:
        s = Scenario(w.client, args.screenshots)
        if args.platform == "offscreen":
            # No window manager: after a menu or dialog the window is not activated again.
            s.require_keyboard_focus = False
        client = w.client
        if real_input:
            real = wayland_real_input.RealInput(w.whale.pid)
            # Wait until the window is mapped, then raise it above everything else.
            for _ in range(50):
                try:
                    print("real input: test window at", real.prepare_window())
                    break
                except wayland_real_input.RealInputUnavailable:
                    time.sleep(0.2)
            else:
                raise Failure("real input: the test window was not found")
        menu = Menu(client, s, popups, real)
        client.tool("set_hidden_files", shown=False)
        client.tool("set_window_size", width=1200, height=800)
        s.expect("folder loaded", {"url": url, "panes.0.loading": False})
        client.tool("set_view", pane=0, view="orcinus_treemap")
        state = s.expect("treemap shown", {"panes.0.viewId": "orcinus_treemap", f"{tm()}.status": "ready",
                                           "focus.role": "plugin:orcinus_treemap"})
        total = state["panes"][0]["treemap"]["totalSize"]

        # A: open a deep file with its application (the test opener), from the menu
        menu.open(u("a/deep.txt"), FILE_MENU)
        s.expect("A: Whale's selection stays empty for a deep entry", {"panes.0.selectionCount": 0,
                                                                        f"{tm()}.selectedDeep": True}, focus=False)
        menu.choose("open")
        wait_until("A: the deep file was opened with its application",
                   lambda: opener_log.exists() and opener_log.read_text().split() == [str(root / "a/deep.txt")])
        s.expect("A: the location did not change", {"url": url, "popupMenu": None, "panes.0.viewId": "orcinus_treemap"})

        # B: show in folder: the folder becomes the location, the file Whale's selection there
        menu.open(u("a/deep.txt"), FILE_MENU)
        menu.choose("show_in_folder")
        s.expect("B: show in folder", {"url": u("a"), "panes.0.selection": [u("a/deep.txt")],
                                       "panes.0.current": u("a/deep.txt"), "panes.0.viewId": "orcinus_treemap",
                                       f"{tm()}.status": "ready", f"{tm()}.selected": u("a/deep.txt"),
                                       f"{tm()}.selectedDeep": False, "focus.role": "plugin:orcinus_treemap"})
        client.tool("set_view", pane=0, view="dolphin")
        s.expect("B: the file list shows it selected", {"panes.0.viewId": "dolphin", "panes.0.selectedNames": ["deep.txt"]})
        client.tool("set_view", pane=0, view="orcinus_treemap")
        client.tool("trigger_action", action="go_back")
        s.expect("B: back to the top", {"url": url, f"{tm()}.status": "ready", "panes.0.viewId": "orcinus_treemap"})

        # C: properties of a deep folder (KDE's dialog), then closed
        menu.open(u("a/sub"), FOLDER_MENU, folder=True)
        menu.choose("properties")
        s.expect("C: the properties dialog of the folder", {"dialogs.0.type": "properties", "dialogs.0.urls": [u("a/sub")]},
                 focus=False)
        client.tool("close_dialogs")
        s.expect("C: closed", {"dialogs": []})

        # D: rename with the dialog of the file list
        menu.open(u("a/rename-me.txt"), FILE_MENU)
        menu.choose("rename")
        s.expect("D: the rename dialog", {"dialogs.0.type": "rename"}, focus=False)
        client.tool("rename_dialog", new_name="renamed.txt")
        state = s.expect("D: renamed; the treemap scanned again and keeps the selection under the new name", {
            "panes.0.fileOperations.last.operation": "rename", "panes.0.fileOperations.last.result": "done",
            "panes.0.fileOperations.last.newUrls": [u("a/renamed.txt")], f"{tm()}.operationScans": 1,
            f"{tm()}.status": "ready", f"{tm()}.selected": u("a/renamed.txt"), "dialogs": []})
        if (root / "a/rename-me.txt").exists() or not (root / "a/renamed.txt").exists():
            raise Failure("D: the file system does not show the rename")

        # E: move to the trash: in a treemap always confirmed (against KDE's default)
        menu.open(u("a/trash-me.txt"), FILE_MENU)
        menu.choose("trash")
        s.expect("E: KIO asks first", {"dialogs.0.buttons": {"$contains": "Move to Trash"}}, focus=False)
        client.tool("answer_dialog", text="Move to Trash")
        s.expect("E: trashed; the treemap scanned again, the selection went away", {
            "panes.0.fileOperations.last.operation": "trash", "panes.0.fileOperations.last.result": "done",
            f"{tm()}.operationScans": 2, f"{tm()}.status": "ready", f"{tm()}.selected": "",
            f"{tm()}.totalSize": total - 30000, "panes.0.selectionCount": 0})
        if (root / "a/trash-me.txt").exists() or not (trash / "trash-me.txt").exists():
            raise Failure("E: the file is not in the profile's trash")
        for other in ("a/deep.txt", "a/renamed.txt", "a/delete-me.txt", "a/split-me.txt", "b/target.txt", "top.txt"):
            if not (root / other).exists():
                raise Failure(f"E: {other} is gone too")

        # F: delete permanently: KIO's confirmation; first canceled, then confirmed
        menu.open(u("a/delete-me.txt"), FILE_MENU)
        menu.choose("delete")
        state = s.expect("F: KIO asks first", {"dialogs.0.buttons": {"$contains": "Delete Permanently"}}, focus=False)
        client.tool("close_dialogs")
        s.expect("F: canceled: nothing happened", {
            "panes.0.fileOperations.last.operation": "delete", "panes.0.fileOperations.last.result": "canceled",
            "dialogs": [], f"{tm()}.operationScans": 2})
        if not (root / "a/delete-me.txt").exists():
            raise Failure("F: deleted although canceled")
        menu.open(u("a/delete-me.txt"), FILE_MENU)
        menu.choose("delete")
        s.expect("F: asked again", {"dialogs.0.buttons": {"$contains": "Delete Permanently"}}, focus=False)
        client.tool("answer_dialog", text="Delete Permanently")
        s.expect("F: deleted", {"panes.0.fileOperations.last.result": "done", f"{tm()}.operationScans": 3,
                                f"{tm()}.status": "ready", "dialogs": []})
        if (root / "a/delete-me.txt").exists() or (trash / "delete-me.txt").exists():
            raise Failure("F: the file was not deleted for good")

        # G: since T2 Whale's own actions act on the deep entry too (test_t2_acceptance.py
        # carries them out); the ones not routed stay off
        client.tool("treemap_select", url=u("a/deep.txt"))
        conditions = {f"actions.{name}.enabled": True for name in ROUTED}
        conditions.update({f"{tm()}.selectedDeep": True, "panes.0.selectionCount": 0, "command.source": "plugin",
                           "command.urls": [u("a/deep.txt")], "actions.compare_files.enabled": False,
                           "actions.show_target.enabled": False, "actions.duplicate.enabled": False,
                           "actions.duplicate.gate": "no destination folder"})
        s.expect("G: Whale's actions act on the deep entry", conditions)

        # K: copy and cut. Only offscreen: elsewhere the clipboard is the user's own (the
        # compositor's), also in a test profile.
        if args.platform == "offscreen":
            menu.open(u("a/deep.txt"), FILE_MENU)
            menu.choose("copy")
            s.expect("K: copied", {"clipboard.urls": [u("a/deep.txt")], "clipboard.cut": False})
            menu.open(u("a/deep.txt"), FILE_MENU)
            menu.choose("cut")
            s.expect("K: cutting asks first", {"dialogs.0.buttons": {"$contains": "Cut"}}, focus=False)
            client.tool("answer_dialog", text="Cut")
            s.expect("K: cut", {"clipboard.urls": [u("a/deep.txt")], "clipboard.cut": True, "dialogs": []})
            # A click into the treemap (offscreen nothing activates the window after a dialog).
            client.tool("treemap_select", url=u("a/deep.txt"))
        else:
            print("skip  K: the clipboard is the user's own outside of offscreen")

        # H: split view: the left treemap's operation leaves the right half alone
        client.tool("set_split_view", enabled=True)
        s.expect("H: split view", {"splitAnimating": False, "splitView": True, "panes.1.loading": False})
        # Dolphin animates opening the split view; real input needs the cells where they stay.
        wait_until("H: the left treemap has its final size", lambda: stable_canvas(client))
        client.tool("set_location", pane=1, url=u("b"))
        s.expect("H: right half at b", {"panes.1.url": u("b"), "panes.1.loading": False})
        client.tool("set_selection", pane=1, urls=[u("b/target.txt")])
        client.tool("activate_pane", pane=1)
        s.expect("H: right half active with its selection", {"activePane": 1, "panes.1.selection": [u("b/target.txt")]})
        menu.open(u("a/split-me.txt"), FILE_MENU)
        menu.choose("trash")
        s.expect("H: KIO asks first", {"dialogs.0.buttons": {"$contains": "Move to Trash"}}, focus=False)
        client.tool("answer_dialog", text="Move to Trash")
        s.expect("H: trashed from the left treemap; the right half unchanged", {
            "panes.0.fileOperations.last.operation": "trash", "panes.0.fileOperations.last.result": "done",
            "panes.0.fileOperations.last.urls": [u("a/split-me.txt")],
            "panes.1.url": u("b"), "panes.1.selection": [u("b/target.txt")], "panes.1.viewId": "dolphin"})
        if (root / "a/split-me.txt").exists() or not (root / "b/target.txt").exists():
            raise Failure("H: wrong files")
        menu.open(u("a/sub"), FOLDER_MENU, folder=True)
        menu.choose("show_in_folder")
        s.expect("H: show in parent folder goes to the treemap's half, which becomes active", {
            "panes.0.url": u("a"), "panes.0.selection": [u("a/sub")], "activePane": 0,
            "focus.role": "plugin:orcinus_treemap", "focus.pane": 0,
            "panes.1.url": u("b"), "panes.1.selection": [u("b/target.txt")]})
        # Closing the split view closes the active half: the right one, the treemap stays.
        client.tool("activate_pane", pane=1)
        client.tool("set_split_view", enabled=False)
        s.expect("H: split view closed", {"splitView": False, "panes.0.viewId": "orcinus_treemap"})

        # I: reload: F5 scans again; a change outside of Whale shows up after it (no stale cache)
        client.tool("set_location", pane=0, url=url)
        state = s.expect("I: back at the top", {"url": url, f"{tm()}.status": "ready"})
        scans = state["panes"][0]["treemap"]["finishedScans"]
        size = state["panes"][0]["treemap"]["totalSize"]
        make_file(root / "b/new.bin", 10000)
        client.tool("trigger_action", action="reload")
        s.expect("I: F5 scans again and sees the new file", {f"{tm()}.finishedScans": scans + 1, f"{tm()}.status": "ready",
                                                             f"{tm()}.totalSize": size + 10000})
        # A menu entry for an entry that is gone meanwhile only offers to show its folder.
        (root / "b/new.bin").unlink()
        if real:
            menu.open(u("b/new.bin"), ["show_in_folder"])
            real.key(wayland_real_input.KEY_ESC)
        elif popups:
            client.tool("treemap_context_menu", url=u("b/new.bin"))
            s.expect("I: an entry gone meanwhile: only 'Show in Folder'", {"popupMenu.names": ["show_in_folder"]},
                     focus=False)
            client.tool("close_popup")
        else:
            menu.open(u("b/new.bin"), ["show_in_folder"])
        s.expect("I: no menu left open", {"popupMenu": None})
    finally:
        if real:
            real.release_window()
        w.kill()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--whale", required=True)
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--plugin-dir", required=True)
    parser.add_argument("--platform", default="offscreen", help="empty for the session's platform")
    parser.add_argument("--popups", choices=["auto", "on", "off", "real"], default="auto",
                        help="open popup menus with synthesized input (on; auto: only offscreen), not at all (off), "
                        "or with real input of the Wayland session through ydotool (real)")
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_T1_PRIVATE_BUS") != "1":
        # See test_m6_acceptance.py: a private session bus, output through a file.
        env = dict(os.environ, WHALE_T1_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
        # Real input talks to KWin on the session's own bus.
        env["WHALE_SESSION_BUS_ADDRESS"] = os.environ.get("DBUS_SESSION_BUS_ADDRESS", "")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-t1-"))
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
    print("T1 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
