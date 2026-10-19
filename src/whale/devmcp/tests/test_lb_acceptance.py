#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the location bar of each file view (location bar plan, LB4), driven
only through the dev MCP. It starts from the profile of a TB6 user and goes on from there:

  M migration: a File View Toolbar layout of TB6 and hidden File View Toolbars; the first
    start adds the location bar (with a backup) and shows the toolbars once; hidden again,
    they stay hidden after a restart
  A one view: the location bar, Ctrl+L, typing a location
  B split view: two location bars, input in one changes only its view, activation
  C history: Back and Forward of the view, the location bar follows
  D plugin view: file list, Cushion Treemap, file list: the same location bar
  E Configure Toolbars: remove, add again, once only, Cancel, Defaults
  F "Show File View Toolbars" off: the location bars exist, hidden
  G tabs: a location bar per view, switching tabs, a tab never shown
  H closing the split view: the closed view's IDs are gone
  I restart: layout, visibility and the migration's state kept

Input goes through the real widgets (location_bar_input, trigger_toolbar_action,
configure_toolbars). Whale runs on a private D-Bus session bus (the script starts one).
Arguments as test_tb_acceptance.py.
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
from mcp_test_support import Failure, Scenario
from test_tb_acceptance import FILE_VIEW_DEFAULT, MAIN_DEFAULT, TREEMAP, Session, check, configure, ids, local_file, toolbar, \
    trigger, trigger_error

# The File View Toolbar file of a TB6 user who changed it (KDE's formatting, a text of their own).
FILE_VIEW_TB6 = (
    '<?xml version="1.0"?>\n'
    '<!DOCTYPE gui SYSTEM "kpartgui.dtd">\n'
    '<gui name="whale_fileview" version="1">\n'
    '    <ToolBar noMerge="1" name="whaleFileViewToolBar">\n'
    '        <text context="@title:menu">File View Toolbar</text>\n'
    '        <Action name="go_back" />\n'
    '        <Action name="go_forward" />\n'
    '        <Action name="go_up" />\n'
    '        <Action name="view_redisplay" />\n'
    '        <Spacer />\n'
    '        <Action name="view_settings" />\n'
    '        <Action name="go_home" />\n'
    '    </ToolBar>\n'
    '    <ActionProperties scheme="Default">\n'
    '        <Action name="go_up" iconText="Upwards"/>\n'
    '    </ActionProperties>\n'
    '</gui>\n').encode()
TB6_MIGRATED = ["go_back", "go_forward", "go_up", "view_redisplay", "whale_location_bar", "|", "view_settings", "go_home"]


def location_bar(state, pane):
    return toolbar(state, pane)["locationBar"]


def instances(state):
    return {i["instanceId"]: i for i in state["toolbars"]["instances"]}


def type_location(client, toolbar_id, text, submit=True):
    return json.loads(client.tool("location_bar_input", toolbar=toolbar_id, text=text, submit=submit)[0]["text"])


def type_error(client, toolbar_id, text):
    error = client.tool_error("location_bar_input", toolbar=toolbar_id, text=text, submit=True)
    check(error is not None, f"location_bar_input {toolbar_id} {text!r} should have failed")
    return error


def start(args, work, folder=None):
    w = Session(args, work, folder)
    s = Scenario(w.client, args.screenshots)
    # Offscreen nothing activates the window again after a dialog; the focus is not the subject.
    s.require_keyboard_focus = False
    return w, w.client, s


def run(args, work):
    root = work / "files"
    for path in ("a/sub", "b/sub2", "c"):
        (root / path).mkdir(parents=True)
    for name, size in (("a/one.txt", 3000), ("b/three.txt", 500), ("c/four.txt", 200)):
        (root / name).write_bytes(b"x" * size)
    # Locations as the views have them after typing a folder: with the "/" of the folder.
    a, b, c = ((root / x).as_uri() + "/" for x in ("a", "b", "c"))
    a_sub, b_sub = (root / "a" / "sub").as_uri() + "/", (root / "b" / "sub2").as_uri() + "/"
    path_a, path_b, path_c = (str(root / x) + "/" for x in ("a", "b", "c"))
    path_a_sub, path_b_sub = str(root / "a" / "sub") + "/", str(root / "b" / "sub2") + "/"

    # The profile of a TB6 user: an own File View Toolbar layout, the File View Toolbars hidden.
    config = work / "profile" / "config"
    config.mkdir(parents=True, exist_ok=True)
    whalerc = config / "whalerc"
    whalerc.write_text("[General]\nGlobalViewProps=false\n\n[WhaleToolbars]\nShowFileViewToolBars=false\n")
    file_view_rc = local_file(work, "whalefileviewtoolbarui.rc")
    backup = file_view_rc.with_name(file_view_rc.name + ".pre-tb7")
    file_view_rc.parent.mkdir(parents=True, exist_ok=True)
    file_view_rc.write_bytes(FILE_VIEW_TB6)

    w, client, s = start(args, work, root / "a")
    try:
        # M: the first start of TB7
        client.tool("set_window_size", width=1600, height=800)
        state = s.expect("M: migrated: toolbars shown, the location bar after Reload", {
            "toolbars.fileViewsVisible": True, "panes.0.toolbar.visible": True, "panes.0.toolbar.initialized": True,
            "panes.0.toolbar.layout": TB6_MIGRATED, "panes.0.toolbar.locationBar.present": True,
            "panes.0.toolbar.locationBar.visible": True, "panes.0.loading": False})
        check(backup.read_bytes() == FILE_VIEW_TB6, "M: the backup is the file of before")
        migrated = file_view_rc.read_bytes()
        check(migrated.count(b"whale_location_bar") == 1 and b'iconText="Upwards"' in migrated, "M: the file", migrated.decode())
        check(migrated == FILE_VIEW_TB6.replace(b'"view_redisplay" />\n', b'"view_redisplay" />\n        <Action name="whale_location_bar"/>\n'),
              "M: nothing else changed")
        rc = whalerc.read_text()
        check("MigratedFileViewLayoutTB7=true" in rc and "MigratedFileViewVisibilityTB7=true" in rc, "M: markers", rc)
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        s.expect("M: hidden again by the user", {"toolbars.fileViewsVisible": False, "panes.0.toolbar.locationBar.visible": False})
        w.quit()
        w, client, s = start(args, work, root / "a")
        s.expect("M: still hidden after the restart, the layout migrated once", {
            "toolbars.fileViewsVisible": False, "panes.0.toolbar.locationBar.exists": True,
            "panes.0.toolbar.locationBar.connected": True, "panes.0.toolbar.locationBar.visible": False,
            "panes.0.toolbar.initialized": False, "panes.0.toolbar.layout": TB6_MIGRATED})
        check(file_view_rc.read_bytes() == migrated and backup.read_bytes() == FILE_VIEW_TB6, "M: no second migration")
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        client.tool("set_window_size", width=1600, height=800)
        # From here on the default layouts.
        configure(client, open=True, select="file-view", defaults=True, ok=True)
        state = s.expect("M: shown, defaults", {"toolbars.fileViewsVisible": True, "panes.0.toolbar.layout": FILE_VIEW_DEFAULT,
                                                "toolbars.global.layout": MAIN_DEFAULT, "panes.0.toolbar.locationBar.visible": True},
                         focus=False)
        check(backup.read_bytes() == FILE_VIEW_TB6, "M: Defaults keep the backup")
        print("ok    M: migration end to end")

        # A: one view
        client.tool("set_location", pane=0, url=a)
        state = s.expect("A: one view at a", {"panes.0.loading": False, "paneCount": 1,
                                              "panes.0.toolbar.locationBar.present": True,
                                              "panes.0.toolbar.locationBar.editable": False,
                                              "panes.0.toolbar.locationBar.active": True}, focus=False)
        bar = location_bar(state, 0)
        check(bar["exists"] and bar["connected"] and bar["visible"], "A: exists, connected, visible", bar)
        check(bar["url"].rstrip("/") == state["panes"][0]["url"].rstrip("/") == a.rstrip("/"), "A: the view's URL", bar)
        key_a = bar["widgetKey"]
        check(key_a.startswith("location-bar-"), "A: widgetKey", bar)
        check(len(state["toolbars"]["instances"]) == 1, "A: one file view")
        client.tool("trigger_action", action="edit_location")
        s.expect("A: Ctrl+L: editable, focused", {"panes.0.toolbar.locationBar.editable": True,
                                                  "panes.0.toolbar.locationBar.focused": True}, focus=False)
        typed = type_location(client, 0, path_a_sub)
        check(typed["text"].startswith(path_a_sub), "A: typed into the editor", typed)
        s.expect("A: typed and submitted: the view is there, breadcrumb again", {
            "panes.0.url": a_sub, "panes.0.toolbar.locationBar.url": a_sub,
            "panes.0.toolbar.locationBar.editable": False, "panes.0.loading": False}, focus=False)
        # Not a location of the editor's list: an editor that loses the focus with such a text
        # goes there (KUrlComboBox/QComboBox), and offscreen the completion popup takes it.
        not_submitted = str(root / "a" / "one.txt")
        type_location(client, 0, not_submitted, submit=False)
        s.expect("A: typed, not submitted: editable with the text, the view stays", {
            "panes.0.url": a_sub, "panes.0.toolbar.locationBar.editable": True,
            "panes.0.toolbar.locationBar.text": {"$contains": not_submitted}}, focus=False)
        check("widget" in trigger_error(client, 0, "whale_location_bar"), "A: the location bar is no command of the toolbar")
        print("ok    A: one view")

        # B: split view
        client.tool("set_split_view", enabled=True)
        client.tool("set_location", pane=1, url=b)
        state = s.expect("B: split, b on the other side", {"splitAnimating": False, "paneCount": 2, "panes.1.loading": False,
                                                            "panes.1.toolbar.locationBar.visible": True}, focus=False)
        bar_a, bar_b = location_bar(state, 0), location_bar(state, 1)
        id_a, id_b = toolbar(state, 0)["instanceId"], toolbar(state, 1)["instanceId"]
        key_b = bar_b["widgetKey"]
        check(bar_a["widgetKey"] == key_a and key_b != key_a, "B: two location bars", [bar_a, bar_b])
        check(bar_a["url"] != bar_b["url"], "B: different locations", [bar_a["url"], bar_b["url"]])
        client.tool("activate_pane", pane=0)
        s.expect("B: A active", {"activePane": 0, "panes.0.toolbar.locationBar.active": True,
                                 "panes.1.toolbar.locationBar.active": False}, focus=False)
        type_location(client, 1, path_b_sub)
        s.expect("B: typed in B: only B goes there, B active", {
            "panes.1.url": b_sub, "panes.1.toolbar.locationBar.url": b_sub, "panes.0.url": a_sub, "activePane": 1,
            "panes.1.toolbar.locationBar.active": True, "panes.0.toolbar.locationBar.active": False}, focus=False)
        type_location(client, id_a, path_c)
        s.expect("B: typed in A (by instanceId): only A goes there, A active", {
            "panes.0.url": c, "panes.0.toolbar.locationBar.url": c, "panes.1.url": b_sub, "activePane": 0}, focus=False)
        print("ok    B: split view")

        # C: history of the view
        type_location(client, 0, path_a)
        s.expect("C: A at a", {"panes.0.url": a, "panes.0.loading": False}, focus=False)
        client.tool("trigger_action", action="go_back")
        s.expect("C: Back (window, active A): c", {"panes.0.url": c, "panes.0.toolbar.locationBar.url": c, "panes.1.url": b_sub},
                 focus=False)
        trigger(client, id_a, "go_back")
        s.expect("C: Back in A's toolbar: a/sub", {"panes.0.url": a_sub, "panes.0.toolbar.locationBar.url": a_sub}, focus=False)
        client.tool("trigger_action", action="go_forward")
        s.expect("C: Forward: c", {"panes.0.url": c, "panes.0.toolbar.locationBar.url": c}, focus=False)
        trigger(client, 1, "go_back")
        s.expect("C: Back in B's toolbar: B's history only", {"panes.1.url": b, "panes.1.toolbar.locationBar.url": b,
                                                              "panes.0.url": c}, focus=False)
        print("ok    C: history")

        # D: plugin view in A
        client.tool("activate_pane", pane=0)
        client.tool("set_view", pane=0, view=TREEMAP)
        state = s.expect("D: A shows the treemap", {"panes.0.viewId": TREEMAP, "panes.0.treemap.status": "ready"}, focus=False)
        check(location_bar(state, 0)["widgetKey"] == key_a and location_bar(state, 0)["url"] == c, "D: the same location bar",
              location_bar(state, 0))
        type_location(client, 0, path_a)
        s.expect("D: typed in A's location bar: the treemap's view goes to a", {
            "panes.0.url": a, "panes.0.toolbar.locationBar.url": a, "panes.1.url": b}, focus=False)
        client.tool("set_view", pane=0, view="dolphin")
        state = s.expect("D: the file list again", {"panes.0.viewId": "dolphin", "panes.0.url": a}, focus=False)
        check(location_bar(state, 0)["widgetKey"] == key_a, "D: still the same location bar")
        print("ok    D: plugin view")

        # E: Configure Toolbars
        dialog = configure(client, open=True, select="file-view")
        check(ids(dialog["current"]).count("whale_location_bar") == 1, "E: in the File View Toolbar once", ids(dialog["current"]))
        check("whale_location_bar" not in ids(dialog["available"]), "E: not offered twice")
        dialog = configure(client, select="global")
        check("url_navigators" not in ids(dialog["available"]) + ids(dialog["current"]), "E: no global location bars", dialog)
        check("whale_location_bar" not in ids(dialog["available"]), "E: not for the main toolbar")
        configure(client, select="file-view", remove="whale_location_bar", apply=True)
        without = [x for x in FILE_VIEW_DEFAULT if x != "whale_location_bar"]
        state = s.expect("E: removed: not placed, still there and connected", {
            "panes.0.toolbar.layout": without, "panes.1.toolbar.layout": without,
            "panes.0.toolbar.locationBar.present": False, "panes.0.toolbar.locationBar.exists": True,
            "panes.0.toolbar.locationBar.connected": True, "panes.0.toolbar.locationBar.visible": False,
            "panes.0.toolbar.locationBar.widgetKey": key_a}, focus=False)
        check("not in the File View Toolbar" in type_error(client, 0, path_b), "E: no input into a location bar not placed")
        dialog = configure(client, insert="whale_location_bar", apply=True)
        check("whale_location_bar" not in ids(dialog["available"]), "E: once only", ids(dialog["available"]))
        state = s.expect("E: added again: the same widgets, at the end", {
            "panes.0.toolbar.layout": without + ["whale_location_bar"], "panes.1.toolbar.layout": without + ["whale_location_bar"],
            "panes.0.toolbar.locationBar.present": True, "panes.0.toolbar.locationBar.visible": True,
            "panes.0.toolbar.locationBar.widgetKey": key_a, "panes.1.toolbar.locationBar.widgetKey": key_b}, focus=False)
        check("not in the available" in client.tool_error("configure_toolbars", insert="whale_location_bar"), "E: no second one")
        configure(client, remove="whale_location_bar")
        configure(client, cancel=True)
        s.expect("E: removed, then Cancel: still placed", {"panes.0.toolbar.locationBar.present": True,
                                                            "panes.0.toolbar.layout": without + ["whale_location_bar"]}, focus=False)
        configure(client, open=True, select="file-view", defaults=True, ok=True)
        state = s.expect("E: Defaults", {"panes.0.toolbar.layout": FILE_VIEW_DEFAULT, "panes.1.toolbar.layout": FILE_VIEW_DEFAULT,
                                         "panes.0.toolbar.locationBar.widgetKey": key_a, "panes.0.toolbar.locationBar.present": True},
                         focus=False)
        print("ok    E: Configure Toolbars")

        # F: File View Toolbars hidden
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        s.expect("F: hidden: the location bars too, still there", {
            "toolbars.fileViewsVisible": False, "panes.0.toolbar.locationBar.visible": False,
            "panes.0.toolbar.locationBar.present": True, "panes.0.toolbar.locationBar.connected": True,
            "panes.1.toolbar.locationBar.visible": False}, focus=False)
        check("not shown" in type_error(client, 0, path_b), "F: no input into a hidden location bar")
        client.tool("trigger_action", action="edit_location")
        s.expect("F: Ctrl+L with hidden location bars: nothing to see, Whale fine", {
            "panes.0.toolbar.locationBar.visible": False, "paneCount": 2}, focus=False)
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        s.expect("F: shown again", {"toolbars.fileViewsVisible": True, "panes.0.toolbar.locationBar.visible": True,
                                    "panes.1.toolbar.locationBar.visible": True}, focus=False)
        print("ok    F: visibility")

        # G: tabs
        client.tool("trigger_action", action="new_tab")
        state = s.expect("G: second tab", {"tabs.count": 2, "tabs.current": 1, "panes.0.toolbar.locationBar.visible": True},
                         focus=False)
        id_tab, key_tab = toolbar(state, 0)["instanceId"], location_bar(state, 0)["widgetKey"]
        keys = {i: x["locationBar"]["widgetKey"] for i, x in instances(state).items()}
        check(len(keys) == 3 and len(set(keys.values())) == 3 and keys[id_a] == key_a and keys[id_b] == key_b,
              "G: a location bar per view", keys)
        type_location(client, id_tab, path_b)
        s.expect("G: input in the new tab's view", {"panes.0.url": b}, focus=False)
        client.tool("trigger_action", action="activate_prev_tab")
        state = s.expect("G: back in the first tab", {"tabs.current": 0, "panes.0.url": a, "panes.1.url": b}, focus=False)
        check({i: x["locationBar"]["widgetKey"] for i, x in instances(state).items()} == keys, "G: switching tabs connects nothing anew")
        check(location_bar(state, 0)["widgetKey"] == key_a, "G: pane 0 has its own again")
        check("not shown" in type_error(client, id_tab, path_c), "G: the other tab's location bar is not shown")
        # A tab never shown: created while the File View Toolbars are hidden.
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        client.tool("trigger_action", action="new_tab")
        state = s.expect("G: a tab never shown with a toolbar", {"tabs.count": 3, "toolbars.fileViewsVisible": False}, focus=False)
        new = [x for i, x in instances(state).items() if i not in keys]
        check(len(new) == 1 and not new[0]["initialized"] and new[0]["locationBar"]["exists"]
              and new[0]["locationBar"]["connected"] and not new[0]["locationBar"]["present"],
              "G: its location bar exists and is connected, not placed; the toolbar stays lazy", new)
        client.tool("trigger_action", action="close_tab")
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        client.tool("trigger_action", action="activate_next_tab")
        s.expect("G: in the second tab", {"tabs.count": 2, "tabs.current": 1}, focus=False)
        client.tool("trigger_action", action="close_tab")
        state = s.expect("G: one tab left", {"tabs.count": 1, "paneCount": 2, "toolbars.fileViewsVisible": True}, focus=False)
        check(id_tab not in instances(state), "G: the closed tab's view is gone")
        check("no file view with the instanceId" in type_error(client, id_tab, path_a), "G: its instanceId is invalid")
        print("ok    G: tabs")

        # H: closing the split view (Whale's default: the active view closes)
        client.tool("activate_pane", pane=1)
        s.expect("H: B active", {"activePane": 1}, focus=False)
        client.tool("set_split_view", enabled=False)
        state = s.expect("H: one view left", {"paneCount": 1, "panes.0.url": a}, focus=False)
        check(list(instances(state)) == [id_a] and location_bar(state, 0)["widgetKey"] == key_a, "H: A and its location bar stay",
              state["toolbars"]["instances"])
        check("no file view with the instanceId" in type_error(client, id_b, path_a), "H: B's instanceId is gone")
        check("no pane" in type_error(client, 1, path_a), "H: no pane 1")
        check("main toolbar" in type_error(client, "global", path_a), "H: the main toolbar has no location bar")
        type_location(client, 0, path_b)
        s.expect("H: the remaining location bar works", {"panes.0.url": b}, focus=False)
        print("ok    H: lifecycle")

        # I: restart with a changed layout
        configure(client, open=True, select="file-view", insert="go_home", ok=True)
        s.expect("I: Home added", {"panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["go_home"]}, focus=False)
        w.quit()
        w, client, s = start(args, work, root / "c")
        state = s.expect("I: restarted: layout, visibility, location bar", {
            "toolbars.fileViewsVisible": True, "panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["go_home"],
            "panes.0.toolbar.locationBar.present": True, "panes.0.toolbar.locationBar.visible": True,
            "panes.0.toolbar.locationBar.url": (root / "c").as_uri()}, focus=False)
        rc = whalerc.read_text()
        check("MigratedFileViewLayoutTB7=true" in rc and "MigratedFileViewVisibilityTB7=true" in rc, "I: markers kept", rc)
        check(backup.read_bytes() == FILE_VIEW_TB6, "I: the backup unchanged")
        check(file_view_rc.read_bytes().count(b"whale_location_bar") == 1, "I: no second migration")
        type_location(client, 0, path_a)
        s.expect("I: input after the restart", {"panes.0.url": a}, focus=False)
        print("ok    I: restart")
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
    if os.environ.get("WHALE_LB_PRIVATE_BUS") != "1":
        # Run again on a private session bus of our own (see test_m6_acceptance.py).
        env = dict(os.environ, WHALE_LB_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-lb-"))
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
    print("LB acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
