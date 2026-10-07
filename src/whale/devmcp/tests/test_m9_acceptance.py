#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the view plugin milestone M9 (the treemap view whale_treemap),
driven only through the dev MCP, with restarts done here as a user would start Whale again.

  A basic treemap        B hover and information   C navigation (folders, back/forward)
  D view state           E history                 F split view, focus, resizing
  G session restart      H cache (hit, invalidation, rescan)
  I deep selection: Whale's actions act on the treemap's own selection (T2)
  L loading and error states

The test trees are made here (sparse files with one block each; no user data). Whale runs
on a private D-Bus session bus, as in test_m6_acceptance.py, and with file operations
allowed, so that a shortcut that got through would really delete or move a file.
"""

import argparse
import json
import os
import pathlib
import random
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance

MB = 1024 * 1024


def make_file(path, size):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        if size:
            # Sparse, with one block: the scanner counts files without blocks as 0 (like /proc).
            f.seek(size - 1)
            f.write(b"x")


def make_nice_tree(root):
    """A tree for looking at: media, photos, documents, archives, code, hidden and empty entries."""
    r = random.Random(42)
    make_file(root / "Videos/holiday-2024.mkv", 900 * MB)
    make_file(root / "Videos/talk.mp4", 350 * MB)
    for i in range(6):
        make_file(root / f"Videos/clips/clip{i:02}.webm", r.randint(10, 60) * MB)
    for year in (2021, 2022, 2023):
        for i in range(25):
            make_file(root / f"Pictures/{year}/IMG_{i:04}.jpg", r.randint(2, 9) * MB)
        make_file(root / f"Pictures/{year}/panorama.png", r.randint(30, 80) * MB)
    for i in range(12):
        make_file(root / f"Music/Album A/track{i:02}.flac", r.randint(20, 45) * MB)
    for i in range(10):
        make_file(root / f"Music/Album B/track{i:02}.mp3", r.randint(5, 12) * MB)
    make_file(root / "Documents/report.pdf", 12 * MB)
    make_file(root / "Documents/thesis.odt", 4 * MB)
    for i in range(40):
        make_file(root / f"Documents/notes/note{i:03}.txt", r.randint(1, 200) * 1024)
    make_file(root / "Archives/backup-2023.tar.gz", 420 * MB)
    make_file(root / "Archives/old-project.zip", 120 * MB)
    make_file(root / "Archives/image.iso", 260 * MB)
    for sub in ("src/core", "src/ui", "src/net", "tests"):
        for i in range(30):
            make_file(root / f"Code/project/{sub}/file{i:02}.cpp", r.randint(2, 80) * 1024)
    make_file(root / "Code/project/build/app", 45 * MB)
    make_file(root / "Code/project/build/libcore.so", 30 * MB)
    make_file(root / ".cache/thumbnails/big.cache", 60 * MB)
    make_file(root / "empty.txt", 0)
    (root / "Empty Folder").mkdir()
    make_file(root / "Zero/zero1", 0)
    make_file(root / "readme.md", 3 * 1024)
    (root / "link-to-videos").symlink_to("Videos")


def make_many(root, files):
    """@p files small files in a few thousand folders (for the loading state)."""
    folders = [root / f"d{a}/d{b}" for a in range(40) for b in range(50)]
    for folder in folders:
        folder.mkdir(parents=True)
    for i in range(files):
        fd = os.open(folders[i % len(folders)] / f"f{i}", os.O_CREAT | os.O_WRONLY, 0o644)
        os.write(fd, b"x")
        os.close(fd)


def scanned_size(root):
    """The total size as the scanner sees it: apparent sizes of files with blocks, links not followed."""
    total = 0
    for folder, folders, files in os.walk(root):  # does not follow links
        for name in files:
            info = os.lstat(os.path.join(folder, name))
            if not os.path.islink(os.path.join(folder, name)) and info.st_blocks > 0:
                total += info.st_size
    return total


class Session:
    """One Whale process with its MCP client, in the profile that survives restarts."""

    def __init__(self, args, work, folder):
        self.whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform,
                                 automation=True, file_operations=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_m9_acceptance", "version": "1"}})
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


def uri(path):
    """A URL as Whale's state shows it (QUrl::toString(): spaces are not encoded)."""
    return urllib.parse.unquote(pathlib.Path(path).as_uri())


def tm(pane=0):
    return f"panes.{pane}.treemap"


def run(args, work):
    root = work / "files" / "nice"
    make_nice_tree(root)
    (work / "files" / "other").mkdir()
    make_file(work / "files" / "other" / "solo.bin", 5 * MB)
    url = uri(root)
    u = lambda path: uri(root / path)
    total = scanned_size(root)

    w = Session(args, work, root)
    try:
        s = Scenario(w.client, args.screenshots)
        client = w.client
        client.tool("set_hidden_files", shown=False)
        client.tool("set_window_size", width=1280, height=800)
        s.expect("folder loaded", {"url": url, "panes.0.loading": False, "window.width": 1280})

        # A: the treemap of the tree
        client.tool("set_view", pane=0, view="whale_treemap")
        state = s.expect("A: treemap shown and scanned", {
            "panes.0.viewId": "whale_treemap", f"{tm()}.status": "ready", f"{tm()}.totalSize": total,
            f"{tm()}.scanRoot": url, "focus.role": "plugin:whale_treemap"})
        if state["panes"][0]["treemap"]["cells"] < 50:
            raise Failure(f"A: only {state['panes'][0]['treemap']['cells']} cells")
        # Sorting, grouping, zoom and previews do not apply to a treemap.
        s.expect("A: list actions disabled", {"actions.sort.gate": "no HostOrder capability",
                                              "actions.view_zoom_in.gate": "no Zoom capability",
                                              "actions.show_preview.gate": "no Previews capability"})

        # B: hover describes deep entries in the information panel
        client.tool("trigger_action", action="show_information_panel")
        deep_file = u("Pictures/2022/panorama.png")
        client.tool("treemap_hover", url=deep_file)
        s.expect("B: hover over a deep file", {f"{tm()}.hovered": deep_file, "shell.itemInfoUrl": deep_file,
                                               "shell.itemInfoIsDir": False})
        client.tool("treemap_hover", url=u("Videos/holiday-2024.mkv"))
        s.expect("B: hover over a listed folder's file", {"shell.itemInfoUrl": u("Videos/holiday-2024.mkv")})
        client.tool("treemap_select", url=u("Music/Album A"))
        s.expect("B: a deep folder selected", {f"{tm()}.selected": u("Music/Album A"), f"{tm()}.selectedDeep": True,
                                               f"{tm()}.selectedShown": True, "panes.0.selectionCount": 0})
        client.tool("treemap_hover")
        s.expect("B: pointer away: the deep selection is described", {f"{tm()}.hovered": "",
                                                                       "shell.itemInfoUrl": u("Music/Album A"),
                                                                       "shell.itemInfoIsDir": True})
        client.tool("treemap_select", url=u("Archives"))
        s.expect("B: a listed folder is Whale's selection too", {f"{tm()}.selectedDeep": False,
                                                                  "panes.0.selectedNames": ["Archives"]})
        client.tool("treemap_key", key="Escape")
        s.expect("B: Escape clears it", {f"{tm()}.selected": "", "panes.0.selectionCount": 0})

        # C: navigation
        client.tool("treemap_activate", url=u("Pictures/2022"))
        s.expect("C: Enter opens the deep folder", {"url": u("Pictures/2022"), f"{tm()}.status": "ready",
                                                    f"{tm()}.scanRoot": u("Pictures/2022"), "panes.0.viewId": "whale_treemap"})
        client.tool("trigger_action", action="go_back")
        s.expect("C: back", {"url": url, f"{tm()}.status": "ready", f"{tm()}.scanRoot": url})
        client.tool("trigger_action", action="go_forward")
        s.expect("C: forward", {"url": u("Pictures/2022"), f"{tm()}.status": "ready"})
        client.tool("trigger_action", action="go_up")
        s.expect("C: up", {"url": {"$contains": "/Pictures"}, f"{tm()}.status": "ready", f"{tm()}.scanRoot": u("Pictures")})
        client.tool("treemap_activate", url=u("Pictures/2023/IMG_0004.jpg"), how="double_click")
        s.expect("C: double click opens the file's folder", {"url": u("Pictures/2023"), f"{tm()}.status": "ready"})
        client.tool("set_location", pane=0, url=url)
        s.expect("C: back at the top", {"url": url, f"{tm()}.status": "ready"})

        # D: the view's own state survives switching views
        client.tool("treemap_zoom", direction="in", url=u("Videos/talk.mp4"))
        client.tool("treemap_set_options", color_mode="topFolder", labels=False)
        client.tool("treemap_select", url=u("Videos/clips/clip03.webm"))
        s.expect("D: zoomed into Videos, top folder colors, no names", {
            f"{tm()}.zoomPath": ["Videos"], f"{tm()}.colorMode": "topFolder", f"{tm()}.showLabels": False,
            f"{tm()}.selected": u("Videos/clips/clip03.webm"), "url": url})
        client.tool("set_view", pane=0, view="dolphin")
        s.expect("D: file list; the treemap's state is kept", {"panes.0.viewId": "dolphin",
                                                               "panes.0.storedViewStates": {"$contains": "whale_treemap"}})
        client.tool("set_view", pane=0, view="whale_treemap")
        s.expect("D: treemap again, as it was", {
            "panes.0.viewId": "whale_treemap", f"{tm()}.status": "ready", f"{tm()}.zoomPath": ["Videos"],
            f"{tm()}.colorMode": "topFolder", f"{tm()}.showLabels": False, f"{tm()}.selected": u("Videos/clips/clip03.webm")})
        client.tool("treemap_set_options", color_mode="fileType", labels=True)
        client.tool("treemap_key", key="-")
        s.expect("D: zoomed out, file type colors", {f"{tm()}.zoomPath": [], f"{tm()}.colorMode": "fileType"})

        # E: history: each location has its own state
        client.tool("treemap_zoom", direction="in", url=u("Pictures/2021/panorama.png"))
        s.expect("E: zoomed into Pictures", {f"{tm()}.zoomPath": ["Pictures"]})
        client.tool("set_location", pane=0, url=u("Music"))
        s.expect("E: Music, unzoomed, no selection", {"url": u("Music"), f"{tm()}.status": "ready", f"{tm()}.zoomPath": [],
                                                      f"{tm()}.selected": ""})
        client.tool("treemap_select", url=u("Music/Album B/track03.mp3"))
        client.tool("trigger_action", action="go_back")
        s.expect("E: back: zoomed into Pictures again", {
            "url": url, f"{tm()}.status": "ready", f"{tm()}.zoomPath": ["Pictures"],
            "panes.0.restore": {"source": "history", "requested": "whale_treemap", "shown": "whale_treemap", "fallback": False}})
        client.tool("trigger_action", action="go_forward")
        s.expect("E: forward: Music with its selection", {"url": u("Music"), f"{tm()}.status": "ready",
                                                          f"{tm()}.selected": u("Music/Album B/track03.mp3")})
        client.tool("trigger_action", action="go_back")
        s.expect("E: and back", {"url": url, f"{tm()}.zoomPath": ["Pictures"]})
        client.tool("treemap_key", key="-")

        # F: split view: treemap left, file list right
        client.tool("set_split_view", enabled=True)
        s.expect("F: split", {"paneCount": 2, "activePane": 1, "panes.1.loading": False})
        client.tool("set_location", pane=1, url=u("Documents"))
        s.expect("F: right: file list in Documents", {"panes.1.url": u("Documents"), "panes.1.viewId": "dolphin",
                                                      "panes.1.loading": False})
        # The halves are equally wide once Dolphin's animation is over.
        time.sleep(0.6)
        before = client.state()["panes"][0]["treemap"]
        client.tool("treemap_select", pane=0, url=u("Code/project/build/app"))
        s.expect("F: a click into the treemap activates the left half", {
            "activePane": 0, "focus.role": "plugin:whale_treemap", "focus.pane": 0,
            f"{tm()}.selected": u("Code/project/build/app"), "actions.deletefile.enabled": True,
            "command.source": "plugin", "command.urls": [u("Code/project/build/app")]})
        client.tool("activate_pane", pane=1)
        s.expect("F: right half active: the file list's actions", {"activePane": 1, "focus.pane": 1,
                                                                    "actions.sort.enabled": True,
                                                                    f"{tm()}.selected": u("Code/project/build/app")})
        client.tool("set_split_ratio", left_share=0.7)
        state = s.expect("F: left wider, laid out again", {f"{tm()}.status": "ready"})
        after = state["panes"][0]["treemap"]
        if not (after["canvasWidth"] > before["canvasWidth"] and after["layoutCount"] > before["layoutCount"]):
            raise Failure(f"F: no new layout after resizing: {before['canvasWidth']} -> {after['canvasWidth']}")
        client.tool("treemap_hover", pane=0, url=u("Archives/image.iso"))
        s.expect("F: hit tests follow the new geometry", {f"{tm()}.hovered": u("Archives/image.iso"),
                                                          f"{tm()}.selectedShown": True})
        client.tool("set_split_ratio", left_share=0.3)
        state = s.expect("F: left narrower", {f"{tm()}.status": "ready"})
        if not state["panes"][0]["treemap"]["canvasWidth"] < after["canvasWidth"]:
            raise Failure("F: the treemap did not get narrower")
        client.tool("treemap_hover", pane=0, url=u("Videos/holiday-2024.mkv"))
        s.expect("F: hover after resizing", {f"{tm()}.hovered": u("Videos/holiday-2024.mkv")})
        client.tool("set_split_ratio", left_share=0.5)

        # G: session restart with the treemap in the split view
        client.tool("treemap_zoom", pane=0, direction="in", url=u("Archives/image.iso"))
        client.tool("treemap_set_options", pane=0, color_mode="topFolder")
        s.expect("G: before quitting", {f"{tm()}.zoomPath": ["Archives"], f"{tm()}.colorMode": "topFolder"}, focus=False)
        w.quit()
        w = Session(args, work, None)
        client = w.client
        s.client = client
        # The automation answers before Dolphin has restored its session.
        s.expect("G: session restored", {"paneCount": 2}, timeout_ms=20000, focus=False)
        s.expect("G: treemap left with its state, file list right", {
            "panes.0.viewId": "whale_treemap", "panes.0.restore.source": "session", f"{tm()}.status": "ready",
            f"{tm()}.zoomPath": ["Archives"], f"{tm()}.colorMode": "topFolder",
            f"{tm()}.selected": u("Code/project/build/app"), "panes.1.viewId": "dolphin",
            "panes.1.url": u("Documents")}, timeout_ms=20000, focus=False)
        # Closing the split view closes the active half: the file list on the right.
        client.tool("activate_pane", pane=1)
        client.tool("set_split_view", enabled=False)
        client.tool("treemap_zoom", direction="out")
        client.tool("treemap_set_options", color_mode="fileType")

        # H: the cache: a hit on return, a real scan after a change
        s.expect("H: one pane, the treemap", {"paneCount": 1, "panes.0.viewId": "whale_treemap", f"{tm()}.status": "ready"})
        stats = client.state()["treeCache"]
        client.tool("set_location", pane=0, url=uri(work / "files" / "other"))
        s.expect("H: elsewhere", {f"{tm()}.status": "ready", f"{tm()}.totalSize": 5 * MB})
        client.tool("set_location", pane=0, url=url)
        state = s.expect("H: back: from the cache", {f"{tm()}.status": "ready", f"{tm()}.totalSize": total,
                                                     "treeCache.hits": stats["hits"] + 1})
        if state["treeCache"]["misses"] != stats["misses"] + 1:  # only "other" was scanned
            raise Failure(f"H: unexpected scans: {stats} -> {state['treeCache']}")
        client.tool("set_location", pane=0, url=uri(work / "files" / "other"))
        s.expect("H: elsewhere again", {f"{tm()}.status": "ready"})
        make_file(root / "Documents/notes/new-big.txt", 77 * MB)
        s.expect("H: the change drops the kept tree", {"treeCache.invalidations": stats["invalidations"] + 1})
        misses = client.state()["treeCache"]["misses"]
        client.tool("set_location", pane=0, url=url)
        s.expect("H: back: scanned again, with the new file", {f"{tm()}.status": "ready",
                                                               f"{tm()}.totalSize": total + 77 * MB,
                                                               "treeCache.misses": misses + 1})
        total += 77 * MB

        # I: since T2 Whale's actions act on the treemap's own selection, never on the hidden
        # file list's (test_t2_acceptance.py carries them out)
        victim = root / "Pictures/2021/IMG_0007.jpg"
        client.tool("treemap_select", url=uri(victim))
        state = s.expect("I: a deep file selected: the actions act on it", {
            f"{tm()}.selected": uri(victim), f"{tm()}.selectedDeep": True, "panes.0.selectionCount": 0,
            "command.source": "plugin", "command.urls": [uri(victim)]})
        for action in ("deletefile", "movetotrash", "edit_cut", "edit_copy", "renamefile", "copy_location"):
            if not state["actions"][action]["enabled"] or state["actions"][action]["gate"] is not None:
                raise Failure(f"I: {action} does not act on the selection: {state['actions'][action]}")
        # Nothing is created in a treemap: no destination folder.
        for action in ("duplicate", "edit_paste", "create_dir"):
            if state["actions"][action]["gate"] != "no destination folder":
                raise Failure(f"I: {action} is not off without a destination: {state['actions'][action]}")
        if state["actions"]["compare_files"]["gate"] != "not routed to a view's own selection":
            raise Failure(f"I: compare_files: {state['actions']['compare_files']}")
        client.tool("treemap_select", url=u("Archives"))
        s.expect("I: a listed folder: Whale's selection too, the same items", {
            "panes.0.selectedNames": ["Archives"], "command.urls": [u("Archives")], "actions.movetotrash.enabled": True})
        client.tool("treemap_key", key="Escape")
        s.expect("I: nothing selected: the actions are off", {"command.urls": [], "actions.movetotrash.enabled": False,
                                                             "actions.movetotrash.gate": "nothing selected"})
        if not victim.exists() or not (root / "Archives").exists():
            raise Failure("I: a file is gone")

        # L: loading and error states
        if args.loading_files:
            many = work / "files" / "many"
            make_many(many, args.loading_files)
            client.tool("set_location", pane=0, url=uri(many))
            loading = json.loads(client.tool("wait_for_state", conditions={
                f"{tm()}.status": "scanning", f"{tm()}.loadingShown": True, f"{tm()}.cells": 0}, timeout_ms=5000)[0]["text"])
            if loading["matched"]:
                s.step_count += 1
                print(f"ok {s.step_count:2} L: a large tree: loading is shown, no old tree ({loading['elapsedMs']} ms)")
                if s.screenshots:
                    client.tool("screenshot", save_path=os.path.join(s.screenshots, f"{s.step_count:02}.png"))
            else:
                treemap = loading["state"]["panes"][0]["treemap"]
                # The message only comes after 150 ms, so a cache hit does not flicker.
                if treemap["status"] != "ready" or treemap["lastScanMs"] >= 150:
                    raise Failure(f"L: no loading state: {json.dumps(loading['mismatches'])}")
                print(f"-- L: the scan took {treemap['lastScanMs']} ms, less than the delay of the loading "
                      "message: not observable on this machine")
            s.expect("L: then the tree", {f"{tm()}.status": "ready", f"{tm()}.totalSize": args.loading_files},
                     timeout_ms=30000)
        locked = work / "files" / "locked"
        make_file(locked / "secret.bin", 1000)
        client.tool("set_location", pane=0, url=uri(locked))
        s.expect("L: a readable folder", {f"{tm()}.status": "ready"})
        client.tool("trigger_action", action="go_back")
        s.expect("L: back", {"url": {"$contains": "files/"}, f"{tm()}.status": "ready"})
        locked.chmod(0)
        try:
            client.tool("set_location", pane=0, url=uri(locked))
            state = s.expect("L: no permission: an error, the view stays usable", {
                f"{tm()}.status": "failed", f"{tm()}.cells": 0, "panes.0.viewId": "whale_treemap"})
            if not state["panes"][0]["treemap"]["error"]:
                raise Failure("L: no error message")
            client.tool("trigger_action", action="go_back")
            s.expect("L: back works", {f"{tm()}.status": "ready", "panes.0.viewId": "whale_treemap"})
        finally:
            locked.chmod(0o755)

        # A non-local location: the file list takes over, back brings the treemap back.
        client.tool("set_location", pane=0, url=url)
        s.expect("L: treemap at the top", {"url": url, f"{tm()}.status": "ready"})
        client.tool("set_location", pane=0, url="trash:/")
        s.expect("L: trash: file list instead", {"url": "trash:/", "panes.0.viewId": "dolphin",
                                                 "panes.0.restore": {"source": "location", "requested": "whale_treemap",
                                                                     "shown": "dolphin", "fallback": True}})
        client.tool_error("set_view", pane=0, view="whale_treemap")  # refused or ignored
        state = client.state()
        if state["panes"][0]["viewId"] != "dolphin":
            raise Failure("L: the treemap was shown for trash:/")
        client.tool("trigger_action", action="go_back")
        s.expect("L: back: the treemap again", {"url": url, "panes.0.viewId": "whale_treemap", f"{tm()}.status": "ready"})
    finally:
        w.kill()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--whale", required=True)
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--plugin-dir", required=True)
    parser.add_argument("--platform", default="offscreen", help="empty for the session's platform")
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--loading-files", type=int, default=300000,
                        help="files of the tree for the loading state (0: skip that step)")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not shutil.which("dbus-run-session"):
        print("skipped: dbus-run-session is missing")
        return 77
    if os.environ.get("WHALE_M9_PRIVATE_BUS") != "1":
        # See test_m6_acceptance.py: a private session bus, output through a file.
        env = dict(os.environ, WHALE_M9_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-m9-"))
    try:
        run(args, work)
    except Failure as failure:
        print(f"FAILED: {failure}")
        print(f"(Whale log: {work / 'profile' / 'whale.log'})")
        args.keep = True
        return 1
    finally:
        if not args.keep:
            locked = work / "files" / "locked"
            if locked.exists():
                locked.chmod(0o755)
            shutil.rmtree(work, ignore_errors=True)
    print("M9 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
