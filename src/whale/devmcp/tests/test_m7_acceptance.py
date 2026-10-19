#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the view plugin milestone M7 (previews, drag and drop), driven
only through the dev MCP.

  A previews   B scrolling   C test list -> file list   D file list -> test list
  E test list -> test list   F drop onto a folder   G several items (and the drop menu)
  H regressions: focus, gating, selections (M3-M6 have their own acceptance tests)

Whale runs in a throwaway profile with WHALE_DEV_MCP_FILE_OPERATIONS=1 and its own
thumbnail cache; all files live in a temporary folder that the test creates, and the
file system is checked after every drop. The drop menu (G, "ask") only runs offscreen:
Wayland compositors show popups only in answer to real input.
Arguments as test_m3_acceptance.py, plus --preview-screenshots DIR for the screenshots
of the preview cases.
"""

import argparse
import json
import os
import pathlib
import shutil
import struct
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, Scenario, start_whale, wait_for_instance


def png(width, height, rgb):
    """A plain PNG of one color (standard library only)."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    rows = b"".join(b"\x00" + bytes(rgb) * width for _ in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def color(i):
    return ((i * 53) % 256, (i * 97) % 256, (i * 151) % 256)


def names(folder):
    return sorted(path.name for path in folder.iterdir())


def run(args, work):
    images = work / "images"
    images.mkdir()
    image_names = [f"img{i}.png" for i in range(6)]
    for i, name in enumerate(image_names):
        (images / name).write_bytes(png(64, 48, color(i)))
    (images / "data.bin").write_bytes(bytes([0, 1, 2, 3, 255, 254]))  # no thumbnailer for this
    many = work / "many"
    many.mkdir()
    many_names = [f"p{i:03}.png" for i in range(400)]
    for i, name in enumerate(many_names):
        (many / name).write_bytes(png(32, 32, color(i)))
    src = work / "src"
    (src / "sub").mkdir(parents=True)
    for name in ("a.txt", "b.txt", "c.txt"):
        (src / name).write_text(name)
    dst = work / "dst"
    dst.mkdir()
    (dst / "existing.txt").write_text("existing")
    other = work / "other"
    other.mkdir()
    ext = work / "ext"
    ext.mkdir()
    for name in ("x.txt", "y.txt", "z.txt"):
        (ext / name).write_text(name)

    def url(path):
        return path.as_uri()

    def check(title, condition, detail):
        if not condition:
            raise Failure(f"{title}: {detail}")
        print(f"ok    {title}")

    whale = start_whale(args.whale, images, work / "profile", args.plugin_dir, args.platform, automation=True,
                        file_operations=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    shots = args.preview_screenshots

    def screenshot(name):
        if shots:
            client.tool("screenshot", save_path=os.path.join(shots, name))

    def preview_state(pane):
        return json.loads(client.tool("get_preview_state")[0]["text"])["panes"][pane]

    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_m7_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        s = Scenario(client, args.screenshots)
        client.tool("set_hidden_files", shown=False)
        client.tool("set_sort", field="name", order="ascending", folders_first=True)
        s.expect("images loaded", {"panes.0.loading": False, "panes.0.itemCount": 7})

        # A: previews in the test list, following Dolphin's "Show Previews"
        if not client.state()["panes"][0]["previews"]["shown"]:
            client.tool("trigger_action", action="show_preview")
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("A: test list with thumbnails of the images, the icon for data.bin", {
            "panes.0.previews.shown": True, "panes.0.previews.capability": True, "panes.0.previews.enabled": True,
            "panes.0.pluginPreviewRows": image_names, "panes.0.previews.runningJobs": 0,
            "actions.show_preview.gate": None, "actions.show_preview.enabled": True}, timeout_ms=20000)
        screenshot("A1-thumbnails.png")
        client.tool("request_preview", pane=0, urls=[url(images / "data.bin"), url(images / "img0.png")], size=64)
        s.expect("A: the provider answers: no preview for data.bin, one for img0.png", {
            "panes.0.previews.pendingCount": 0})
        results = json.loads(client.tool("get_preview_state")[0]["text"])["results"]
        check("A: data.bin without preview (fallback), img0.png with one",
              {"pane": 0, "name": "data.bin", "preview": False, "width": 0, "height": 0} in results
              and any(r["name"] == "img0.png" and r["preview"] and 0 < r["width"] <= 64 for r in results), results)
        client.tool("trigger_action", action="show_preview")
        s.expect("A: previews off: icons, provider disabled", {
            "panes.0.previews.shown": False, "panes.0.previews.enabled": False, "panes.0.pluginPreviewRows": [],
            "panes.0.previews.pendingCount": 0})
        screenshot("A2-icons.png")
        client.tool("trigger_action", action="show_preview")
        s.expect("A: previews on again", {"panes.0.previews.enabled": True, "panes.0.pluginPreviewRows": image_names},
                 timeout_ms=20000)
        screenshot("A3-thumbnails-again.png")

        # B: a large folder: only the rows in sight (and a page around them) get previews.
        # The provider's counters run on over locations; B counts from here.
        before = preview_state(0)
        client.tool("set_location", pane=0, url=url(many))
        s.expect("B: 400 images listed", {"panes.0.loading": False, "panes.0.itemCount": 400})
        state = s.expect("B: the first rows have previews, nothing is pending", {
            "panes.0.pluginPreviewRows": {"$contains": "p000.png"}, "panes.0.previews.runningJobs": 0}, timeout_ms=30000)
        visible = state["panes"][0]["pluginVisibleRows"]
        s.expect("B: the last visible row too", {"panes.0.pluginPreviewRows": {"$contains": visible["last"]},
                                                "panes.0.previews.runningJobs": 0}, timeout_ms=30000)
        page = visible["count"]
        previews = preview_state(0)
        requested = previews["requested"] - before["requested"]
        check(f"B: only {requested} of 400 requested (page of {page} rows)",
              requested <= 2 * page + 1 and previews["maxPending"] <= 2 * page + 1, previews)
        screenshot("B1-top.png")
        client.tool("scroll_to_item", pane=0, url=url(many / "p200.png"))
        state = s.expect("B: scrolled: the new rows get previews, the old ones are dropped", {
            "panes.0.pluginTopItem": "p200.png", "panes.0.pluginPreviewRows": {"$contains": "p200.png"},
            "panes.0.previews.runningJobs": 0}, timeout_ms=30000)
        rows = state["panes"][0]["pluginPreviewRows"]
        check("B: rows out of sight lost their previews", "p000.png" not in rows and "p399.png" not in rows, rows)
        s.expect("B: the last visible row has its preview",
                 {"panes.0.pluginPreviewRows": {"$contains": state["panes"][0]["pluginVisibleRows"]["last"]}}, timeout_ms=30000)
        screenshot("B2-scrolled.png")
        for top in ("p050.png", "p300.png", "p120.png", "p390.png"):
            client.tool("scroll_to_item", pane=0, url=url(many / top))
        state = s.expect("B: after scrolling around, the work settles", {
            "panes.0.previews.runningJobs": 0, "panes.0.previews.pendingCount": 0}, timeout_ms=30000)
        previews = state["panes"][0]["previews"]
        requested = previews["requested"] - before["requested"]
        check(f"B: no flood: {requested} of 400 requested, at most {previews['maxPending']} pending, "
              f"{previews['canceled'] - before['canceled']} canceled",
              requested < 400 and previews["maxPending"] <= 3 * page + 1, previews)

        # C: test list (left) -> file list (right)
        client.tool("set_location", pane=0, url=url(src))
        s.expect("C: src in the test list", {"panes.0.loading": False, "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"]})
        client.tool("set_split_view", enabled=True)
        s.expect("C: split", {"splitAnimating": False, "paneCount": 2, "panes.1.loading": False})
        client.tool("set_location", pane=1, url=url(dst))
        s.expect("C: dst in the file list on the right", {"panes.1.loading": False, "panes.1.viewId": "dolphin",
                                                        "panes.1.items": ["existing.txt"]})
        client.tool("activate_pane", pane="left")
        client.tool("set_selection", pane=0, urls=[url(src / "a.txt")], current=url(src / "a.txt"))
        s.expect("C: a.txt selected in the test list", {"activePane": 0, "panes.0.pluginSelection": ["a.txt"]})
        reply = json.loads(client.tool("perform_drag_drop", source_pane=0, target_pane=1, action="copy")[0]["text"])
        check("C: the test list started the drag of its selection",
              reply["source"] == "plugin-drag-gesture" and reply["dragged"] == ["a.txt"] and reply["dropped"], reply)
        s.expect("C: copied into the right half, which is active now", {
            "panes.1.items": ["a.txt", "existing.txt"], "activePane": 1, "focus.role": "dolphin-item-view",
            "focus.pane": 1, "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"],
            "panes.0.selectedNames": ["a.txt"]})
        check("C: file system after copy", names(src) == ["a.txt", "b.txt", "c.txt", "sub"]
              and names(dst) == ["a.txt", "existing.txt"], (names(src), names(dst)))
        client.tool("activate_pane", pane="left")
        client.tool("set_selection", pane=0, urls=[url(src / "b.txt")], current=url(src / "b.txt"))
        s.expect("C: b.txt selected", {"activePane": 0, "panes.0.pluginSelection": ["b.txt"]})
        client.tool("perform_drag_drop", source_pane=0, target_pane=1, action="move")
        s.expect("C: moved: gone on the left, selection consistent", {
            "panes.1.items": ["a.txt", "b.txt", "existing.txt"], "panes.0.pluginRows": ["sub", "a.txt", "c.txt"],
            "panes.0.selectedNames": [], "panes.0.pluginSelection": [], "activePane": 1})
        check("C: file system after move", names(src) == ["a.txt", "c.txt", "sub"]
              and names(dst) == ["a.txt", "b.txt", "existing.txt"], (names(src), names(dst)))

        # D: file list (left) -> test list (right), into the location
        client.tool("set_view", pane=0, view="dolphin")
        client.tool("set_view", pane=1, view="whale_testlist")
        client.tool("activate_pane", pane="left")
        client.tool("set_selection", pane=0, urls=[url(src / "c.txt")], current=url(src / "c.txt"))
        s.expect("D: c.txt selected in the file list", {"panes.0.viewId": "dolphin", "panes.1.viewId": "whale_testlist",
                                                       "panes.0.selectedNames": ["c.txt"], "activePane": 0})
        reply = json.loads(client.tool("perform_drag_drop", source_pane=0, target_pane=1, action="copy")[0]["text"])
        check("D: the file list's drag data", reply["source"] == "file-list-drag-data" and reply["dropped"], reply)
        s.expect("D: copied into the test list's location, selected there, focus in the test list", {
            "panes.1.pluginRows": ["a.txt", "b.txt", "c.txt", "existing.txt"], "panes.1.selectedNames": ["c.txt"],
            "panes.1.pluginSelection": ["c.txt"], "activePane": 1, "focus.role": "plugin:whale_testlist",
            "focus.pane": 1, "panes.0.selectedNames": ["c.txt"],
            "actions.view_zoom_in.gate": "no Zoom capability", "actions.show_preview.gate": None})
        check("D: file system", names(src) == ["a.txt", "c.txt", "sub"]
              and names(dst) == ["a.txt", "b.txt", "c.txt", "existing.txt"], (names(src), names(dst)))

        # E: test list -> test list, another folder on the right
        client.tool("set_view", pane=0, view="whale_testlist")
        client.tool("set_location", pane=1, url=url(other))
        s.expect("E: both test lists, other on the right", {
            "panes.0.viewId": "whale_testlist", "panes.1.viewId": "whale_testlist", "panes.1.loading": False,
            "panes.1.pluginRows": []})
        client.tool("activate_pane", pane="left")
        client.tool("set_selection", pane=0, urls=[url(src / "a.txt")], current=url(src / "a.txt"))
        s.expect("E: a.txt selected on the left", {"panes.0.pluginSelection": ["a.txt"], "activePane": 0})
        client.tool("perform_drag_drop", source_pane=0, target_pane=1, action="copy")
        s.expect("E: copied to the right only", {
            "panes.1.pluginRows": ["a.txt"], "panes.0.pluginRows": ["sub", "a.txt", "c.txt"], "activePane": 1,
            "focus.role": "plugin:whale_testlist", "focus.pane": 1, "panes.0.pluginSelection": ["a.txt"]})
        check("E: file system: only the right folder changed", names(other) == ["a.txt"]
              and names(src) == ["a.txt", "c.txt", "sub"] and names(dst) == ["a.txt", "b.txt", "c.txt", "existing.txt"],
              (names(src), names(other), names(dst)))

        # F: onto a folder row of the same test list
        client.tool("activate_pane", pane="left")
        client.tool("set_selection", pane=0, urls=[url(src / "c.txt")], current=url(src / "c.txt"))
        s.expect("F: c.txt selected", {"panes.0.pluginSelection": ["c.txt"], "activePane": 0})
        client.tool("perform_drag_drop", source_pane=0, target_pane=0, target_url=url(src / "sub"), action="move")
        s.expect("F: moved into sub, the location stays", {
            "panes.0.pluginRows": ["sub", "a.txt"], "panes.0.url": url(src), "activePane": 0,
            "focus.role": "plugin:whale_testlist", "focus.pane": 0})
        check("F: file system", names(src / "sub") == ["c.txt"] and names(src) == ["a.txt", "sub"],
              (names(src), names(src / "sub")))
        client.tool("set_selection", pane=0, urls=[url(src / "a.txt")], current=url(src / "a.txt"))
        s.expect("F: a.txt selected", {"panes.0.pluginSelection": ["a.txt"]})
        reply = json.loads(client.tool("perform_drag_drop", source_pane=0, target_pane=1, target_url=url(other / "a.txt"),
                                       action="copy")[0]["text"])
        check("F: a plain file is no drop target (as in the file list)",
              not reply["accepted"] and not reply["dropped"] and names(other) == ["a.txt"], (reply, names(other)))

        # G: several items, and the drop menu
        client.tool("set_location", pane=0, url=url(ext))
        s.expect("G: ext on the left", {"panes.0.loading": False, "panes.0.pluginRows": ["x.txt", "y.txt", "z.txt"]})
        client.tool("activate_pane", pane="left")
        client.tool("set_selection", pane=0, urls=[url(ext / n) for n in ("x.txt", "y.txt", "z.txt")],
                    current=url(ext / "x.txt"))
        s.expect("G: three selected", {"panes.0.pluginSelection": ["x.txt", "y.txt", "z.txt"]})
        reply = json.loads(client.tool("perform_drag_drop", source_pane=0, target_pane=1, action="move")[0]["text"])
        check("G: one drag with all three", reply["dragged"] == ["x.txt", "y.txt", "z.txt"], reply)
        s.expect("G: all three moved to the right", {
            "panes.1.pluginRows": ["a.txt", "x.txt", "y.txt", "z.txt"], "panes.0.pluginRows": [],
            "panes.1.selectedNames": ["x.txt", "y.txt", "z.txt"], "activePane": 1})
        check("G: file system", names(ext) == [] and names(other) == ["a.txt", "x.txt", "y.txt", "z.txt"],
              (names(ext), names(other)))
        if args.platform == "offscreen":
            client.tool("set_selection", pane=1, urls=[url(other / "y.txt")], current=url(other / "y.txt"))
            s.expect("G: y.txt selected on the right", {"panes.1.pluginSelection": ["y.txt"]})
            client.tool("perform_drag_drop", source_pane=1, target_pane=0, action="ask")
            s.expect("G: without modifiers KDE's drop menu asks", {
                "popupMenu.texts.0": {"$contains": "Here"}}, focus=False)
            texts = [t.split("\t")[0] for t in client.state()["popupMenu"]["texts"]]
            check("G: the menu offers move, copy and link", {"Move Here", "Copy Here", "Link Here"} <= set(texts), texts)
            client.tool("choose_popup_entry", text="Link Here")
            s.require_keyboard_focus = False  # nothing activates the window again offscreen
            s.expect("G: linked into ext", {"popupMenu": None, "panes.0.pluginRows": ["y.txt"]})
            check("G: a symbolic link", (ext / "y.txt").is_symlink() and (other / "y.txt").exists(), names(ext))
            s.require_keyboard_focus = True
        else:
            print("skip  G (drop menu): popups need real input on this platform (covered offscreen and by "
                  "whaledragdroptest)")

        # H: no regressions of the other milestones' rules
        client.tool("activate_pane", pane="left")
        s.expect("H: left active, test list gating, no focus anomaly", {
            "activePane": 0, "focus.role": "plugin:whale_testlist", "focus.pane": 0,
            "actions.view_zoom_in.gate": "no Zoom capability", "panes.0.focusProxy": "plugin-widget",
            "panes.1.focusProxy": "plugin-widget", "panes.0.hiddenDolphinViewHasFocus": False,
            "panes.1.hiddenDolphinViewHasFocus": False})
        client.tool("set_view", pane=0, view="dolphin")
        client.tool("set_view", pane=0, view="whale_testlist")
        client.tool("set_split_view", enabled=False)
        s.expect("H: back to one pane", {"paneCount": 1, "panes.0.viewId": "whale_testlist"})
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
    parser.add_argument("--preview-screenshots", help="directory for the screenshots of the preview cases")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        print("skipped: no D-Bus session bus")
        return 77
    for directory in (args.screenshots, args.preview_screenshots):
        if directory:
            os.makedirs(directory, exist_ok=True)

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-m7-"))
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
    print("M7 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
