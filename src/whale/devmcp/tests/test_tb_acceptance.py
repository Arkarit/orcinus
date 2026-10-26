#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the multi-toolbar work (TB6), driven only through the dev MCP:
the main toolbar and the File View Toolbars as the user sees and uses them.

  A one view: both toolbars, default layouts, no shortcuts on a view's actions
  B split view: two File View Toolbars, separate instances and action objects
  C targeting: a File View Toolbar acts on its own view and activates it
  D the main toolbar acts on the active view
  E per view state: file list and Cushion Treemap, options, gates, selection
  F menus: history menus of the own view, view settings entries
  G Configure Toolbars: lists, Apply, Cancel, Apply then Cancel, Defaults
  H "Show File View Toolbars": hidden toolbars stay configurable
  I tabs: separate instances, closing a tab, its instanceId is gone
  J restart: layout and visibility kept, File View Toolbars not shown are not built

Buttons are clicked with mouse events through the window (trigger_toolbar_action), the
dialog is operated through its widgets (configure_toolbars). Whale runs on a private D-Bus
session bus (the script starts one) and is restarted like O3. Arguments as
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

TREEMAP = "orcinus_treemap"
MAIN_DEFAULT = ["|", "split_view", "split_stash", "toggle_search", "hamburger_menu"]
FILE_VIEW_DEFAULT = ["go_back", "go_forward", "go_up", "view_redisplay", "whale_location_bar", "view_settings"]


class Session:
    """One Whale process with its MCP client, in the profile that survives restarts."""

    def __init__(self, args, work, folder=None):
        self.whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True)
        self.client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(self.whale.pid)))
        self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                           "clientInfo": {"name": "test_tb_acceptance", "version": "1"}})
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
        raise Failure(message + ("" if detail is None else ": " + json.dumps(detail, indent=1)[:3000]))


def toolbar(state, pane):
    return state["panes"][pane]["toolbar"]


def action(toolbar_state, action_id):
    for entry in toolbar_state["actions"]:
        if entry["id"] == action_id:
            return entry
    raise Failure(f"no action {action_id} in the toolbar: {[a['id'] for a in toolbar_state['actions']]}")


def entry(menu, name):
    """An entry of a menu in the state, by its id or text."""
    for item in menu:
        if item.get("id") == name or item.get("text") == name:
            return item
    raise Failure(f"no menu entry {name!r} in {[i.get('id') or i.get('text') for i in menu]}")


def view_modes(menu):
    """The view modes of a view settings menu: in it, or in its "View Modes" submenu (more than five)."""
    for item in menu:
        if item.get("text") == "View Modes" and "menu" in item:
            return item["menu"]
    return menu


def trigger(client, toolbar_id, action_id, menu_entry=None):
    args = {"toolbar": toolbar_id, "id": action_id}
    if menu_entry is not None:
        args["menu_entry"] = menu_entry
    client.tool("trigger_toolbar_action", **args)


def trigger_error(client, toolbar_id, action_id, menu_entry=None):
    args = {"toolbar": toolbar_id, "id": action_id}
    if menu_entry is not None:
        args["menu_entry"] = menu_entry
    error = client.tool_error("trigger_toolbar_action", **args)
    check(error is not None, f"trigger_toolbar_action {args} should have failed")
    return error


def configure(client, **request):
    return json.loads(client.tool("configure_toolbars", **request)[0]["text"])["dialog"]


def ids(items):
    return [item["id"] for item in items]


def local_file(work, name):
    return work / "profile" / "data" / "kxmlgui5" / "orcinus" / name


def run(args, work):
    root = work / "files"
    for path in ("a/sub", "b/sub2", "c"):
        (root / path).mkdir(parents=True)
    for name, size in (("a/one.txt", 3000), ("a/two.txt", 1000), ("a/.hidden", 10), ("b/three.txt", 500), ("c/four.txt", 200)):
        (root / name).write_bytes(b"x" * size)
    a, b, c = (root / "a").as_uri(), (root / "b").as_uri(), (root / "c").as_uri()
    a_sub, b_sub = (root / "a" / "sub").as_uri(), (root / "b" / "sub2").as_uri()
    # Independent view properties per folder: the two views must not share sorting.
    (work / "profile" / "config").mkdir(parents=True, exist_ok=True)
    (work / "profile" / "config" / "orcinusrc").write_text("[General]\nGlobalViewProps=false\n")
    file_view_rc = local_file(work, "whalefileviewtoolbarui.rc")
    main_rc = local_file(work, "dolphinui.rc")

    w = Session(args, work, root)
    try:
        client = w.client
        s = Scenario(client, args.screenshots)
        # Offscreen nothing activates the window again after a dialog; the focus is not the subject.
        s.require_keyboard_focus = False

        # A: one view, a window with room for the buttons of two File View Toolbars
        client.tool("set_window_size", width=1400, height=800)
        client.tool("set_location", pane=0, url=a)
        state = s.expect("A: one view at a", {"url": a, "panes.0.loading": False, "paneCount": 1,
                                              "toolbars.global.visible": True, "panes.0.toolbar.visible": True,
                                              "panes.0.toolbar.initialized": True})
        check(state["toolbars"]["global"]["layout"] == MAIN_DEFAULT, "A: main toolbar default", state["toolbars"]["global"]["layout"])
        tb_a = toolbar(state, 0)
        check(tb_a["layout"] == FILE_VIEW_DEFAULT, "A: File View Toolbar default", tb_a["layout"])
        check([x["id"] for x in tb_a["actions"]] == [x for x in FILE_VIEW_DEFAULT if x != "|"], "A: actions in layout order")
        check(tb_a["ownerView"] == "dolphin" and tb_a["shown"], "A: owner view, shown", tb_a)
        check(all(x["shortcuts"] == [] for x in tb_a["actions"]), "A: no shortcuts on the view's actions", tb_a["actions"])
        check(state["toolbarDialog"] == {"open": False}, "A: no dialog")
        print("ok    A: layouts, order, no shortcuts")

        # B: split view
        client.tool("set_split_view", enabled=True)
        client.tool("set_location", pane=1, url=b)
        state = s.expect("B: split, b on the other side", {"splitAnimating": False, "paneCount": 2, "panes.1.url": b, "panes.1.loading": False,
                                                            "panes.1.toolbar.initialized": True})
        tb_a, tb_b = toolbar(state, 0), toolbar(state, 1)
        id_a, id_b = tb_a["instanceId"], tb_b["instanceId"]
        check(id_a != id_b and id_a.startswith("view-") and id_b.startswith("view-"), "B: two instanceIds", [id_a, id_b])
        check(tb_a["layout"] == tb_b["layout"] == FILE_VIEW_DEFAULT, "B: same layout")
        keys_a = {x["actionKey"] for x in tb_a["actions"]}
        keys_b = {x["actionKey"] for x in tb_b["actions"]}
        keys_global = {x["actionKey"] for x in state["toolbars"]["global"]["actions"]}
        check(not keys_a & keys_b and not (keys_a | keys_b) & keys_global, "B: separate action objects")
        instances = {i["instanceId"]: i for i in state["toolbars"]["instances"]}
        check(instances[id_a]["pane"] == 0 and instances[id_b]["pane"] == 1, "B: instances", instances)
        print("ok    B: separate instances and action objects")

        # C: targeting, with history in both views
        client.tool("set_location", pane=0, url=a_sub)
        client.tool("set_location", pane=1, url=b_sub)
        s.expect("C: a/sub and b/sub2", {"panes.0.url": a_sub, "panes.1.url": b_sub})
        client.tool("activate_pane", pane=1)
        s.expect("C: B active", {"activePane": 1})
        trigger(client, id_a, "go_back")
        s.expect("C: Back in A's toolbar: only A goes back, A is active", {"panes.0.url": a, "panes.1.url": b_sub, "activePane": 0})
        trigger(client, 1, "go_back")
        s.expect("C: Back in B's toolbar: only B goes back, B is active", {"panes.0.url": a, "panes.1.url": b, "activePane": 1})
        trigger(client, id_a, "go_forward")
        s.expect("C: Forward in A's toolbar", {"panes.0.url": a_sub, "panes.1.url": b, "activePane": 0})

        # D: the main toolbar acts on the active view
        error = trigger_error(client, "global", "go_back")
        check("not in this toolbar" in error, "D: Back is not on the main toolbar by default", error)
        dialog = configure(client, open=True, select="global", insert="go_back", ok=True)
        check(not dialog["open"], "D: dialog closed with OK", dialog)
        state = s.expect("D: Back on the main toolbar", {"toolbars.global.layout": MAIN_DEFAULT + ["go_back"]})
        client.tool("activate_pane", pane=1)
        s.expect("D: B active", {"activePane": 1})
        client.tool("set_location", pane=1, url=b_sub)
        s.expect("D: B at b/sub2", {"panes.1.url": b_sub})
        trigger(client, "global", "go_back")
        s.expect("D: the main toolbar's Back: the active view B", {"panes.1.url": b, "panes.0.url": a_sub, "activePane": 1})
        client.tool("activate_pane", pane=0)
        trigger(client, "global", "go_back")
        s.expect("D: the main toolbar's Back: now the active view A", {"panes.0.url": a, "panes.1.url": b, "activePane": 0})

        # E: per view state: A file list (details, hidden files), B Cushion Treemap
        client.tool("trigger_action", action="view_details")
        client.tool("set_hidden_files", pane=0, shown=True)
        client.tool("set_view", pane=1, view=TREEMAP)
        state = s.expect("E: A details with hidden files, B treemap", {
            "panes.0.viewMode": "details", "panes.0.hiddenFiles": True, "panes.1.viewId": TREEMAP,
            "panes.1.treemap.status": "ready"})
        tb_a, tb_b = toolbar(state, 0), toolbar(state, 1)
        check(tb_a["ownerView"] == "dolphin" and tb_b["ownerView"] == TREEMAP, "E: owner views", [tb_a["ownerView"], tb_b["ownerView"]])
        menu_a, menu_b = action(tb_a, "view_settings")["menu"], action(tb_b, "view_settings")["menu"]
        modes_a, modes_b = view_modes(menu_a), view_modes(menu_b)
        check(entry(modes_a, "details")["checked"] and not entry(modes_b, "details")["checked"], "E: view mode per view")
        check(entry(modes_b, "Cushion Treemap")["checked"] and not entry(modes_a, "Cushion Treemap")["checked"], "E: treemap only in B")
        hidden_a, hidden_b = entry(menu_a, "show_hidden_files"), entry(menu_b, "show_hidden_files")
        check(hidden_a["enabled"] and hidden_a["checked"], "E: Show Hidden Files in A", hidden_a)
        check(not hidden_b["enabled"], "E: Show Hidden Files gated in the treemap only", hidden_b)
        flat_a, flat_b = entry(menu_a, "whale_include_subfolders"), entry(menu_b, "whale_include_subfolders")
        check(flat_a["enabled"] and not flat_a["checked"], "E: flat in A", flat_a)
        check(not flat_b["enabled"], "E: flat gated in the treemap", flat_b)
        # Selection actions: on the toolbars through Configure Toolbars, enabled only with a selection.
        configure(client, open=True, select="file-view", insert=["show_hidden_files", "renamefile"], ok=True)
        client.tool("set_selection", pane=0, urls=[(root / "a" / "one.txt").as_uri()])
        state = s.expect("E: Rename on both File View Toolbars, a file selected in A", {
            "panes.0.selectionCount": 1, "panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["show_hidden_files", "renamefile"],
            "panes.1.toolbar.layout": FILE_VIEW_DEFAULT + ["show_hidden_files", "renamefile"]})
        rename_a, rename_b = action(toolbar(state, 0), "renamefile"), action(toolbar(state, 1), "renamefile")
        check(rename_a["enabled"] and not rename_b["enabled"], "E: Rename per view", [rename_a, rename_b])
        error = trigger_error(client, 1, "renamefile")
        check("disabled" in error, "E: disabled action refused", error)
        hidden_button_b = action(toolbar(state, 1), "show_hidden_files")
        check(not hidden_button_b["enabled"] and hidden_button_b["gateReason"], "E: gated button in the treemap view", hidden_button_b)
        check(action(toolbar(state, 0), "show_hidden_files")["enabled"], "E: the same button enabled in A")
        error = trigger_error(client, 1, "show_hidden_files")
        check("disabled" in error and hidden_button_b["gateReason"] in error, "E: a gated button is refused with its reason", error)
        trigger(client, 0, "show_hidden_files")
        s.expect("E: Show Hidden Files in A's toolbar: only A", {"panes.0.hiddenFiles": False, "activePane": 0})
        configure(client, open=True, select="file-view", remove="show_hidden_files", ok=True)
        print("ok    E: modes, options, gates and selection per view")

        # F: menus
        client.tool("set_view", pane=1, view="dolphin")
        client.tool("set_location", pane=1, url=c)
        s.expect("F: B back in the file list at c", {"panes.1.viewId": "dolphin", "panes.1.url": c, "panes.1.loading": False})
        error = trigger_error(client, id_a, "go_back", str(root / "c"))
        check("no entry" in error, "F: A's history menu does not know B's locations", error)
        trigger(client, id_a, "go_back", str(root))
        s.expect("F: A's history menu: back to the start folder; B stays", {"panes.0.url": root.as_uri(), "panes.1.url": c,
                                                                            "activePane": 0})
        state = client.state()
        # The menu as it was filled when it opened: A's locations.
        back_menu = [x["text"] for x in action(toolbar(state, 0), "go_back")["menu"]]
        check(back_menu == [str(root)],
              "F: A's history menu has only A's locations", back_menu)
        trigger(client, 1, "view_settings", "sort > sort_by_size")
        s.expect("F: Sort By Size from B's view settings: only B", {"panes.1.sort.field": "size", "panes.0.sort.field": "name",
                                                                    "activePane": 1})
        trigger(client, 1, "view_settings", "sort > sort_by_text")
        s.expect("F: back to name", {"panes.1.sort.field": "name"})
        check("no entry" in trigger_error(client, 0, "view_settings", "sort > nonsense"), "F: unknown entry")
        check("widget" in trigger_error(client, 0, "view_settings", "Zoom"), "F: the zoom slider is no command")
        print("ok    F: history and view settings menus")

        # G: Configure Toolbars
        dialog = configure(client, open=True, select="file-view")
        check(dialog["open"] and dialog["selected"] == "file-view" and dialog["toolBars"] == ["global", "file-view"], "G: dialog", dialog)
        check(ids(dialog["current"]) == FILE_VIEW_DEFAULT + ["renamefile"], "G: current of the File View Toolbar", ids(dialog["current"]))
        available = ids(dialog["available"])
        check("go_home" in available and "split_view" not in available and "url_navigators" not in available,
              "G: available of the File View Toolbar", available)
        dialog = configure(client, select="global")
        check(dialog["selected"] == "global" and ids(dialog["current"]) == MAIN_DEFAULT + ["go_back"], "G: main toolbar", dialog)
        check("split_view" not in ids(dialog["available"]), "G: available of the main toolbar")
        configure(client, select="file-view")
        before = file_view_rc.read_bytes()
        dialog = configure(client, insert="go_home", apply=True)
        state = s.expect("G: Apply: both File View Toolbars", {"panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["renamefile", "go_home"],
                                                               "panes.1.toolbar.layout": FILE_VIEW_DEFAULT + ["renamefile", "go_home"]},
                         focus=False)
        check(file_view_rc.read_bytes() != before and b"go_home" in file_view_rc.read_bytes(), "G: Apply saved")
        applied = file_view_rc.read_bytes()
        dialog = configure(client, insert="stop")
        check(dialog["modified"], "G: a change to apply", dialog)
        dialog = configure(client, cancel=True)
        check(not dialog["open"], "G: cancelled")
        state = s.expect("G: Cancel: nothing changed", {"panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["renamefile", "go_home"]}, focus=False)
        check(file_view_rc.read_bytes() == applied, "G: Cancel saved nothing")
        configure(client, open=True, select="file-view", insert="view_zoom_in", apply=True)
        configure(client, insert="stop")
        configure(client, cancel=True)
        state = s.expect("G: Apply, change, Cancel: the applied change stays", {
            "panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["renamefile", "go_home", "view_zoom_in"],
            "panes.1.toolbar.layout": FILE_VIEW_DEFAULT + ["renamefile", "go_home", "view_zoom_in"]}, focus=False)
        check(b"view_zoom_in" in file_view_rc.read_bytes() and b'"stop"' not in file_view_rc.read_bytes(), "G: file after Apply+Cancel")
        check("not in the available" in client.tool_error("configure_toolbars", open=True, select="file-view", insert="split_view"),
              "G: an action the File View Toolbar cannot show")
        dialog = configure(client, defaults=True)
        check(dialog["open"], "G: the dialog stays open after Defaults", dialog)
        state = s.expect("G: Defaults: both toolbars", {"toolbars.global.layout": MAIN_DEFAULT,
                                                        "panes.0.toolbar.layout": FILE_VIEW_DEFAULT,
                                                        "panes.1.toolbar.layout": FILE_VIEW_DEFAULT}, focus=False)
        check(not file_view_rc.exists() and not main_rc.exists(), "G: Defaults removed both user files")
        configure(client, cancel=True)
        print("ok    G: Configure Toolbars")

        # H: "Show File View Toolbars" off; the toolbars stay configurable
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        s.expect("H: hidden", {"toolbars.fileViewsVisible": False, "panes.0.toolbar.visible": False,
                               "panes.1.toolbar.visible": False, "toolbars.global.visible": True}, focus=False)
        check("not shown" in trigger_error(client, 0, "go_up"), "H: a hidden toolbar cannot be clicked")
        configure(client, open=True, select="file-view", insert="go_home", ok=True)
        s.expect("H: configured while hidden: still hidden, new layout", {
            "panes.0.toolbar.visible": False, "panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["go_home"]}, focus=False)
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        s.expect("H: shown again with the new layout", {
            "toolbars.fileViewsVisible": True, "panes.0.toolbar.visible": True, "panes.0.toolbar.shown": True,
            "panes.1.toolbar.shown": True, "panes.1.toolbar.layout": FILE_VIEW_DEFAULT + ["go_home"]}, focus=False)
        trigger(client, 1, "go_up")
        s.expect("H: Up in B's toolbar", {"panes.1.url": root.as_uri() + "/", "panes.0.url": root.as_uri(), "activePane": 1}, focus=False)

        # I: tabs
        client.tool("trigger_action", action="new_tab")
        state = s.expect("I: second tab", {"tabs.count": 2, "tabs.current": 1, "panes.0.toolbar.initialized": True}, focus=False)
        id_tab = toolbar(state, 0)["instanceId"]
        all_ids = [i["instanceId"] for i in state["toolbars"]["instances"]]
        check(id_tab not in (id_a, id_b) and len(set(all_ids)) == len(all_ids) == 3, "I: own instance in the new tab", all_ids)
        trigger(client, id_tab, "go_up")
        trigger(client, id_tab, "go_up")
        check("not shown" in trigger_error(client, id_a, "go_up"), "I: the other tab's toolbar is not shown")
        client.tool("trigger_action", action="close_tab")
        state = s.expect("I: tab closed", {"tabs.count": 1}, focus=False)
        check(id_tab not in [i["instanceId"] for i in state["toolbars"]["instances"]], "I: its instance is gone")
        check("no file view with the instanceId" in trigger_error(client, id_tab, "go_up"), "I: its instanceId is invalid")
        check("unknown toolbar" in client.tool_error("configure_toolbars", select="nonsense"), "I: unknown toolbar in the dialog")
        check("unknown action" in trigger_error(client, id_a, "split_view"), "I: not an action of a File View Toolbar")
        check("unknown action" in trigger_error(client, "global", "no_such_action"), "I: unknown action")
        check("no pane" in trigger_error(client, 5, "go_up"), "I: unknown pane")
        print("ok    I: tabs and invalid instances")

        # J: restart: hidden toolbars and a changed layout; two tabs
        configure(client, open=True, select="file-view", insert="stop", ok=True)
        client.tool("trigger_action", action="new_tab")
        s.expect("J: two tabs", {"tabs.count": 2}, focus=False)
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        s.expect("J: hidden before the restart", {"toolbars.fileViewsVisible": False}, focus=False)
        w.quit()
        w = Session(args, work)
        client = w.client
        s = Scenario(client, args.screenshots)
        s.require_keyboard_focus = False
        state = s.expect("J: restarted with both tabs, hidden", {
            "tabs.count": 2, "toolbars.fileViewsVisible": False, "panes.0.toolbar.visible": False,
            "panes.0.toolbar.initialized": False, "panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["go_home", "stop"]}, focus=False)
        check(all(not i["initialized"] for i in state["toolbars"]["instances"]), "J: no File View Toolbar built while hidden",
              state["toolbars"]["instances"])
        client.tool("trigger_action", action="whale_show_file_view_toolbars")
        state = s.expect("J: shown: the current tab's toolbar is built", {
            "panes.0.toolbar.initialized": True, "panes.0.toolbar.layout": FILE_VIEW_DEFAULT + ["go_home", "stop"]}, focus=False)
        current = state["tabs"]["current"]
        others = [i for i in state["toolbars"]["instances"] if i["tab"] != current]
        check(others and all(not i["initialized"] for i in others), "J: the tab never shown is still not built", others)
        check(b'"stop"' in file_view_rc.read_bytes(), "J: layout file")
        print("ok    J: restart")
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
    if os.environ.get("WHALE_TB_PRIVATE_BUS") != "1":
        # Run again on a private session bus of our own (see test_m6_acceptance.py).
        env = dict(os.environ, WHALE_TB_PRIVATE_BUS="1", PYTHONUNBUFFERED="1")
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

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-tb-"))
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
    print("TB acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
