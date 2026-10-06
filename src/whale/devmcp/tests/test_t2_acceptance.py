#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of T2 (Whale's global actions and shortcuts on a plugin view's own
selection, here the treemap's deep selection), driven only through the dev MCP.

  A command source            B open (Enter)              C rename (F2)
  D trash (Delete), delete (Shift+Delete, KIO's confirmation)
  E copy/cut and paste in the other half (offscreen: the clipboard is the user's elsewhere)
  F properties (Alt+Return, the menu action)
  G split view: the active half's selection, always
  H view switch: Treemap -> file list -> Treemap
  I copy/move to the other half      J safety (nothing selected, read-only, gone)
  L duplicate (Ctrl+D, the menu action)

Shortcuts are pressed in the treemap through the window's shortcuts (treemap_key) and the
menu/toolbar path is trigger_action: both are the same QActions. With --real-keys (a
Plasma/Wayland session) F2 and Delete are real key presses of the session (ydotool, see
wayland_real_input.py), sent only while the test window is active and the treemap has the
focus. The test tree is made here; Whale runs on a private D-Bus session bus with file
operations allowed, a home and trash of its own, KDE's default confirmations and a test opener.
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

ROUTED = ["movetotrash", "deletefile", "renamefile", "edit_cut", "edit_copy", "duplicate", "copy_location"]


def make_file(path, size=20000):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"t" * size)


def make_tree(base):
    """The tree of the T2 plan: root with deep entries, target beside it."""
    root = base / "root"
    for name in ("open", "rename-me", "trash-me", "delete-me", "copy-me", "move-me", "dup-me", "cross-copy",
                 "cross-move", "gone"):
        make_file(root / f"deep/{name}.txt")
    make_file(root / "deep/sub/inner.txt")
    make_file(root / "ro/locked.txt")
    make_file(root / "top.txt")
    make_file(base / "target/existing.txt", 100)
    return root, base / "target"


def uri(path):
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


class Session:
    def __init__(self, args, work, folder, opener_log):
        self.whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True,
                                 opener_log=opener_log, file_operations=True, confirm_delete=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_t2_acceptance", "version": "1"}})
        self.client.notify("notifications/initialized")
        wait_for_instance(self.client, self.whale)

    def kill(self):
        self.client.close()
        if self.whale.poll() is None:
            self.whale.terminate()
            self.whale.wait(timeout=10)


def run(args, work):
    root, target = make_tree(work / "files")
    opener_log = work / "opened.log"
    trash = work / "profile" / "data" / "Trash" / "files"
    url = uri(root)
    u = lambda path: uri(root / path)
    offscreen = args.platform == "offscreen"

    w = Session(args, work, root, opener_log)
    real = None
    try:
        s = Scenario(w.client, args.screenshots)
        if offscreen:
            s.require_keyboard_focus = False
        client = w.client
        if args.real_keys:
            real = wayland_real_input.RealInput(w.whale.pid)
            for _ in range(50):
                try:
                    print("real keys: test window at", real.prepare_window())
                    break
                except wayland_real_input.RealInputUnavailable:
                    time.sleep(0.2)
            else:
                raise Failure("real keys: the test window was not found")

        def key(name, real_code=None):
            """A shortcut in the treemap: a real key press if asked for, else through the window's shortcuts."""
            if real and real_code is not None:
                # Only into the active test window with the treemap focused.
                s.expect(f"ready for the real key {name}", {"window.active": True, "focus.role": "plugin:whale_treemap"})
                real.key(real_code)
            else:
                result = json.loads(client.tool("treemap_key", key=name)[0]["text"])
                if name not in ("Return", "Escape") and not result.get("shortcut"):
                    raise Failure(f"{name} reached no shortcut")

        def clear_selection():
            # A click into the treemap first (offscreen nothing activates the window after a dialog).
            client.tool("treemap_select", url=u("deep/open.txt"))
            client.tool("treemap_key", key="Escape")

        client.tool("set_hidden_files", shown=False)
        client.tool("set_window_size", width=1200, height=800)
        s.expect("folder loaded", {"url": url, "panes.0.loading": False})
        client.tool("set_view", pane=0, view="whale_treemap")
        s.expect("treemap shown", {"panes.0.viewId": "whale_treemap", f"{tm()}.status": "ready"})

        # A: the command source
        s.expect("A: nothing selected: the treemap is the source, the actions are off", {
            "command.source": "plugin", "command.urls": [], "actions.movetotrash.enabled": False,
            "actions.movetotrash.gate": "nothing selected"})
        client.tool("treemap_select", url=u("deep/open.txt"))
        conditions = {f"actions.{name}.enabled": True for name in ROUTED}
        conditions.update({"command.source": "plugin", "command.urls": [u("deep/open.txt")], "panes.0.selectionCount": 0,
                           f"{tm()}.selectedDeep": True, "actions.compare_files.enabled": False})
        s.expect("A: a deep file: the treemap's selection, the actions act on it", conditions)

        # B: Enter opens (the file with its application, a folder as the location)
        key("Return")
        wait_until("B: Enter opened the deep file", lambda: opener_log.exists()
                   and opener_log.read_text().split() == [str(root / "deep/open.txt")])
        s.expect("B: the location stayed", {"url": url})
        client.tool("treemap_select", url=u("deep/sub"))
        key("Return")
        s.expect("B: Enter entered the deep folder", {"url": u("deep/sub"), f"{tm()}.status": "ready"})
        client.tool("trigger_action", action="go_back")
        s.expect("B: back", {"url": url, f"{tm()}.status": "ready"})

        # C: F2 renames with the dialog
        client.tool("treemap_select", url=u("deep/rename-me.txt"))
        key("F2", wayland_real_input.KEY_F2)
        s.expect("C: F2 opened the rename dialog for the deep file", {"dialogs.0.type": "rename"}, focus=False)
        client.tool("rename_dialog", new_name="renamed.txt")
        s.expect("C: renamed", {"panes.0.fileOperations.last.operation": "rename",
                                "panes.0.fileOperations.last.result": "done", "dialogs": [],
                                f"{tm()}.selected": u("deep/renamed.txt")})
        if (root / "deep/rename-me.txt").exists() or not (root / "deep/renamed.txt").exists():
            raise Failure("C: wrong files")

        # D: Delete trashes, Shift+Delete deletes after KIO's question
        client.tool("treemap_select", url=u("deep/trash-me.txt"))
        key("Delete", wayland_real_input.KEY_DELETE)
        s.expect("D: Delete trashed it", {"panes.0.fileOperations.last.operation": "trash",
                                          "panes.0.fileOperations.last.result": "done",
                                          "panes.0.fileOperations.last.urls": [u("deep/trash-me.txt")],
                                          f"{tm()}.status": "ready", "command.urls": []})
        if (root / "deep/trash-me.txt").exists() or not (trash / "trash-me.txt").exists():
            raise Failure("D: not in the trash")
        client.tool("treemap_select", url=u("deep/delete-me.txt"))
        key("Shift+Delete")
        s.expect("D: Shift+Delete asks", {"dialogs.0.buttons": {"$contains": "Delete Permanently"}}, focus=False)
        client.tool("close_dialogs")
        s.expect("D: canceled", {"panes.0.fileOperations.last.result": "canceled", "dialogs": []})
        if not (root / "deep/delete-me.txt").exists():
            raise Failure("D: deleted although canceled")
        client.tool("treemap_select", url=u("deep/delete-me.txt"))
        key("Shift+Delete")
        s.expect("D: asked again", {"dialogs.0.buttons": {"$contains": "Delete Permanently"}}, focus=False)
        client.tool("answer_dialog", text="Delete Permanently")
        s.expect("D: deleted", {"panes.0.fileOperations.last.operation": "delete",
                                "panes.0.fileOperations.last.result": "done", "dialogs": []})
        if (root / "deep/delete-me.txt").exists() or (trash / "delete-me.txt").exists():
            raise Failure("D: not deleted for good")
        for other in ("deep/open.txt", "deep/copy-me.txt", "top.txt"):
            if not (root / other).exists():
                raise Failure(f"D: {other} is gone too")

        # F: properties: the deep entry (Alt+Return), the location without a selection (the menu action)
        client.tool("treemap_select", url=u("deep/open.txt"))
        key("Alt+Return")
        s.expect("F: the deep entry's properties", {"dialogs.0.type": "properties", "dialogs.0.urls": [u("deep/open.txt")]},
                 focus=False)
        client.tool("close_dialogs")
        s.expect("F: closed", {"dialogs": []})
        clear_selection()
        client.tool("trigger_action", action="properties")
        s.expect("F: nothing selected: the location's properties", {"dialogs.0.type": "properties",
                                                                    "dialogs.0.urls": [url]}, focus=False)
        client.tool("close_dialogs")
        s.expect("F: closed again", {"dialogs": []})

        # L: duplicate (Ctrl+D, and the menu action)
        client.tool("treemap_select", url=u("deep/dup-me.txt"))
        key("Ctrl+D")
        wait_until("L: Ctrl+D duplicated the deep file", lambda: (root / "deep/dup-me copy.txt").exists())
        client.tool("treemap_select", url=u("deep/dup-me.txt"))
        client.tool("trigger_action", action="duplicate")
        wait_until("L: the menu action duplicated it again (numbered)",
                   lambda: len(list((root / "deep").glob("dup-me*"))) == 3)

        # J: safety
        clear_selection()
        s.expect("J: nothing selected: off", {"command.urls": [], "actions.deletefile.enabled": False})
        if "disabled" not in (client.tool_error("trigger_action", action="trash") or ""):
            raise Failure("J: trash without a selection was not refused")
        if not client.state()["panes"][0]["selectionCount"] == 0:
            raise Failure("J: a selection appeared")
        (root / "ro").chmod(0o555)
        try:
            client.tool("treemap_select", url=u("ro/locked.txt"))
            s.expect("J: read-only: nothing that removes it, copy and properties yes", {
                "command.urls": [u("ro/locked.txt")], "actions.movetotrash.enabled": False,
                "actions.deletefile.enabled": False, "actions.renamefile.enabled": False, "actions.edit_cut.enabled": False,
                "actions.edit_copy.enabled": True, "actions.properties.enabled": True})
            for action in ("trash", "delete", "rename"):
                if "disabled" not in (client.tool_error("trigger_action", action=action) or ""):
                    raise Failure(f"J: {action} on a read-only entry was not refused")
            key("Delete")
            time.sleep(0.3)
            if not (root / "ro/locked.txt").exists():
                raise Failure("J: the read-only entry is gone")
        finally:
            (root / "ro").chmod(0o755)
        client.tool("treemap_select", url=u("deep/gone.txt"))
        (root / "deep/gone.txt").unlink()
        client.tool("treemap_select", url=u("deep/open.txt"))  # select again: the actions are updated
        client.tool("treemap_select", url=u("deep/gone.txt"))  # still a cell (the snapshot)
        s.expect("J: an entry gone meanwhile: nothing acts on it", {
            "command.urls": [u("deep/gone.txt")], "actions.movetotrash.enabled": False, "actions.edit_copy.enabled": False,
            "actions.movetotrash.gate": "not supported for the selection"})
        clear_selection()

        # H: view switch: the source follows at once
        client.tool("treemap_select", url=u("deep/open.txt"))
        client.tool("set_view", pane=0, view="dolphin")
        s.expect("H: the file list: Dolphin's selection (empty)", {"panes.0.viewId": "dolphin", "command.source": "file-list",
                                                                   "command.urls": []}, timeout_ms=0)
        client.tool("set_view", pane=0, view="whale_treemap")
        s.expect("H: the treemap again, with its selection", {"command.source": "plugin",
                                                              "command.urls": [u("deep/open.txt")],
                                                              "actions.movetotrash.enabled": True})

        # G: split view: always the active half's selection
        client.tool("set_split_view", enabled=True)
        s.expect("G: split view", {"splitView": True, "panes.1.loading": False})
        client.tool("set_location", pane=1, url=uri(target))
        s.expect("G: right at target", {"panes.1.url": uri(target), "panes.1.loading": False})
        client.tool("set_selection", pane=1, urls=[uri(target / "existing.txt")])
        client.tool("activate_pane", pane=1)
        s.expect("G: right active: the file list's selection", {"activePane": 1, "command.source": "file-list",
                                                                "command.urls": [uri(target / "existing.txt")],
                                                                "panes.0.command.urls": [u("deep/open.txt")]})
        client.tool("treemap_select", pane=0, url=u("deep/cross-copy.txt"))
        s.expect("G: a click into the treemap: the left half's selection", {
            "activePane": 0, "command.source": "plugin", "command.urls": [u("deep/cross-copy.txt")],
            "panes.1.command.urls": [uri(target / "existing.txt")], "focus.role": "plugin:whale_treemap"})

        # I: copy/move to the other half: the treemap's entry, into the right half's folder
        client.tool("trigger_action", action="copy_to_other_pane")
        wait_until("I: copied to the other half", lambda: (target / "cross-copy.txt").exists())
        if not (root / "deep/cross-copy.txt").exists() or (root / "existing.txt").exists():
            raise Failure("I: wrong source or direction")
        client.tool("treemap_select", pane=0, url=u("deep/cross-move.txt"))
        client.tool("trigger_action", action="move_to_other_pane")
        wait_until("I: moved to the other half", lambda: (target / "cross-move.txt").exists()
                   and not (root / "deep/cross-move.txt").exists())
        s.expect("I: halves stay where they are", {"panes.0.url": url, "panes.1.url": uri(target), "activePane": 0})

        # E: copy/cut and paste in the other half (offscreen only)
        if offscreen:
            client.tool("treemap_select", pane=0, url=u("deep/copy-me.txt"))
            key("Ctrl+C")
            s.expect("E: Ctrl+C put the deep file on the clipboard", {"clipboard.urls": [u("deep/copy-me.txt")],
                                                                     "clipboard.cut": False})
            client.tool("activate_pane", pane=1)
            client.tool("trigger_action", action="paste")
            wait_until("E: pasted in the other half", lambda: (target / "copy-me.txt").exists())
            client.tool("treemap_select", pane=0, url=u("deep/move-me.txt"))
            key("Ctrl+X")
            s.expect("E: Ctrl+X", {"clipboard.urls": [u("deep/move-me.txt")], "clipboard.cut": True})
            client.tool("activate_pane", pane=1)
            client.tool("trigger_action", action="paste")
            wait_until("E: moved by pasting", lambda: (target / "move-me.txt").exists()
                       and not (root / "deep/move-me.txt").exists())
        else:
            print("skip  E: the clipboard is the user's own outside of offscreen")
        if not (target / "existing.txt").exists():
            raise Failure("the right half's own file is gone")
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
    parser.add_argument("--real-keys", action="store_true", help="F2 and Delete as real keys of a Wayland session (ydotool)")
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_T2_PRIVATE_BUS") != "1":
        env = dict(os.environ, WHALE_T2_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-t2-"))
    try:
        run(args, work)
    except Failure as failure:
        print(f"FAILED: {failure}")
        print(f"(Whale log: {work / 'profile' / 'whale.log'})")
        args.keep = True
        return 1
    finally:
        if not args.keep:
            ro = work / "files" / "root" / "ro"
            if ro.exists():
                ro.chmod(0o755)
            shutil.rmtree(work, ignore_errors=True)
    print("T2 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
