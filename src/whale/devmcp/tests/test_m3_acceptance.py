#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the view plugin milestone M3, driven only through the dev MCP.

Starts a real Whale (own profile, own test folder) with WHALE_DEV_MCP=1, talks to it
through the MCP adapter over stdio exactly like an MCP client, and walks through the
M3 acceptance list: test list view, navigation, sorting, hidden files, name filter,
split view with Dolphin and the test list, pane activation, focus, view switching.
A second Whale without WHALE_DEV_MCP must not offer the automation.

  test_m3_acceptance.py --whale BIN --adapter ADAPTER --plugin-dir DIR
                        [--platform offscreen|wayland|xcb] [--screenshots DIR]

Exit code 77 means "skipped" (no D-Bus session bus).
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

FORBIDDEN_ACTIONS = {"delete", "trash", "move_to_trash", "cut", "copy", "paste", "duplicate", "create_dir"}


class Failure(Exception):
    pass


class McpClient:
    def __init__(self, adapter, env):
        self.process = subprocess.Popen([sys.executable, adapter], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        env=env, text=True)
        self.next_id = 0

    def request(self, method, params=None):
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            message["params"] = params
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        response = json.loads(self.process.stdout.readline())
        if "error" in response:
            raise Failure(f"{method}: {response['error']}")
        return response["result"]

    def notify(self, method):
        self.process.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self.process.stdin.flush()

    def tool(self, name, **arguments):
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        if result["isError"]:
            raise Failure(f"{name}{arguments}: {result['content'][0]['text']}")
        return result["content"]

    def tool_error(self, name, **arguments):
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        return result["content"][0]["text"] if result["isError"] else None

    def state(self):
        return json.loads(self.tool("get_ui_state")[0]["text"])

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=10)


class Scenario:
    def __init__(self, client, screenshots):
        self.client = client
        self.screenshots = screenshots
        self.step_count = 0

    def expect(self, title, conditions, timeout_ms=10000):
        """Waits inside Whale until the conditions hold. No focus anomaly is ever allowed."""
        self.step_count += 1
        conditions = dict(conditions)
        conditions.setdefault("focus.anomalies", [])
        # The focus must be real keyboard focus, not only the focus widget of an inactive window.
        conditions.setdefault("focus.keyboardFocus", True)
        result = json.loads(self.client.tool("wait_for_state", conditions=conditions, timeout_ms=timeout_ms)[0]["text"])
        if not result["matched"]:
            raise Failure(f"step {self.step_count} '{title}': " + json.dumps(result["mismatches"], indent=1))
        print(f"ok {self.step_count:2} {title} ({result['elapsedMs']} ms)")
        if self.screenshots:
            path = os.path.join(self.screenshots, f"{self.step_count:02}.png")
            self.client.tool("screenshot", save_path=path)
        return result["state"]


def start_whale(binary, folder, profile, plugin_dir, platform, automation):
    env = dict(os.environ)
    for name in ("config", "data", "cache", "state"):
        (profile / name).mkdir(parents=True, exist_ok=True)
    env.update({
        "XDG_CONFIG_HOME": str(profile / "config"),
        "XDG_DATA_HOME": str(profile / "data"),
        "XDG_CACHE_HOME": str(profile / "cache"),
        "XDG_STATE_HOME": str(profile / "state"),
        "QT_PLUGIN_PATH": plugin_dir + os.pathsep + env.get("QT_PLUGIN_PATH", ""),
    })
    if platform:
        env["QT_QPA_PLATFORM"] = platform
    if automation:
        env["WHALE_DEV_MCP"] = "1"
    else:
        env.pop("WHALE_DEV_MCP", None)
    log = open(profile / "whale.log", "w")
    return subprocess.Popen([binary, "--new-window", str(folder)], env=env, stdout=log, stderr=subprocess.STDOUT)


def wait_for_instance(client, whale, timeout=30):
    """Process start-up only: retries until the new Whale offers the automation."""
    deadline = time.monotonic() + timeout
    while True:
        if whale.poll() is not None:
            raise Failure(f"Whale exited with {whale.returncode}")
        if client.tool_error("get_focus_state") is None:
            return
        if time.monotonic() > deadline:
            raise Failure("Whale did not offer the automation in time")
        time.sleep(0.2)


def run(args, work):
    folder = work / "files"
    (folder / "sub" / "deeper").mkdir(parents=True)
    (folder / "a.txt").write_bytes(b"a" * 300)
    (folder / "b.txt").write_bytes(b"b" * 100)
    (folder / "c.txt").write_bytes(b"c" * 200)
    (folder / ".hidden").write_bytes(b"h")
    (folder / "sub" / "inner.txt").write_bytes(b"i")
    folder_url = folder.as_uri()
    sub_url = (folder / "sub").as_uri()

    whale = start_whale(args.whale, folder, work / "profile", args.plugin_dir, args.platform, automation=True)
    plain = None
    env = dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid))
    client = McpClient(args.adapter, env)
    try:
        init = client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                             "clientInfo": {"name": "test_m3_acceptance", "version": "1"}})
        assert init["serverInfo"]["name"] == "whale-dev", init
        client.notify("notifications/initialized")
        tools = {tool["name"]: tool for tool in client.request("tools/list")["tools"]}
        offered = set(tools["trigger_action"]["inputSchema"]["properties"]["action"]["enum"])
        if offered & FORBIDDEN_ACTIONS:
            raise Failure(f"destructive actions offered: {offered & FORBIDDEN_ACTIONS}")
        # Exactly this small set; anything new must be a deliberate decision (see README.md).
        expected_tools = {"get_ui_state", "get_focus_state", "set_view", "activate_pane", "set_split_view",
                          "set_location", "set_filter_text", "clear_filter", "set_hidden_files", "set_sort",
                          "trigger_action", "wait_for_state", "screenshot"}
        if set(tools) != expected_tools:
            raise Failure(f"unexpected tool set: {sorted(set(tools) ^ expected_tools)}")
        wait_for_instance(client, whale)
        print("ok    MCP initialized, tools:", ", ".join(sorted(tools)))

        # Runtime switch: a Whale started without WHALE_DEV_MCP offers nothing. Started only now:
        # two Whales starting at the same moment wait for each other on D-Bus (up to 25 s each),
        # because a starting Whale asks all running instances for their windows.
        # Always offscreen: on a real desktop it would take the activation from the Whale under test.
        plain = start_whale(args.whale, folder, work / "profile-plain", args.plugin_dir, "offscreen", automation=False)
        if json.loads(client.tool("get_ui_state")[0]["text"])["pid"] != whale.pid:
            raise Failure("the adapter talks to the wrong Whale")
        plain_ready = subprocess.run(["gdbus", "wait", "--session", "--timeout", "30", f"io.github.arkarit.whale-{plain.pid}"],
                                     capture_output=True)
        if plain_ready.returncode != 0:
            raise Failure("the Whale without automation did not start")
        gate = subprocess.run([sys.executable, args.adapter, "--pid", str(plain.pid), "call", "get_ui_state"],
                              capture_output=True, text=True, env=dict(os.environ))
        if gate.returncode == 0 or "no Whale instance" not in gate.stderr:
            raise Failure(f"Whale without WHALE_DEV_MCP answered: {gate.stdout} {gate.stderr}")
        print("ok    Whale without WHALE_DEV_MCP offers no automation")
        for tool, arguments in (("trigger_action", {"action": "delete"}), ("trigger_action", {"action": "paste"})):
            if client.tool_error(tool, **arguments) is None:
                raise Failure(f"{tool}{arguments} was accepted")
        print("ok    destructive actions are refused")

        s = Scenario(client, args.screenshots)
        # Start from defaults; Whale shares the view properties of all folders.
        client.tool("set_hidden_files", shown=False)
        client.tool("set_sort", field="name", order="ascending", folders_first=True)
        state = s.expect("folder loaded in the file list", {
            "paneCount": 1, "panes.0.viewId": "dolphin", "panes.0.loading": False,
            "panes.0.items": ["sub", "a.txt", "b.txt", "c.txt"], "focus.role": "dolphin-item-view"})
        if "whale_testlist" not in state["availableViews"]:
            raise Failure(f"no test list view: {state['availableViews']}")

        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("test list shown", {
            "panes.0.viewId": "whale_testlist", "panes.0.dolphinViewVisible": False,
            "panes.0.pluginWidgetVisible": True, "panes.0.focusProxy": "plugin-widget",
            "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"], "focus.role": "plugin:whale_testlist"})

        client.tool("set_location", pane=0, url=sub_url)
        s.expect("navigated into a folder", {
            "url": sub_url, "panes.0.loading": False, "panes.0.pluginRows": ["deeper", "inner.txt"],
            "panes.0.viewId": "whale_testlist", "focus.role": "plugin:whale_testlist"})
        client.tool("trigger_action", action="go_back")
        s.expect("back in the history", {
            "panes.0.loading": False, "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"],
            "focus.role": "plugin:whale_testlist"})

        client.tool("set_sort", pane=0, field="size", order="ascending", folders_first=False)
        state = s.expect("sorted by size", {"panes.0.sort": {"field": "size", "order": "ascending", "foldersFirst": False}})
        files = [row for row in state["panes"][0]["pluginRows"] if row != "sub"]
        if files != ["b.txt", "c.txt", "a.txt"]:
            raise Failure(f"size order in the test list: {files}")
        client.tool("set_sort", pane=0, field="name", order="descending", folders_first=True)
        s.expect("sorted by name, descending", {"panes.0.pluginRows": ["sub", "c.txt", "b.txt", "a.txt"]})
        client.tool("set_sort", pane=0, order="ascending")
        s.expect("sorted by name, ascending", {"panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"]})

        client.tool("set_hidden_files", pane=0, shown=True)
        s.expect("hidden files shown", {"panes.0.hiddenFiles": True, "panes.0.pluginRows": ["sub", ".hidden", "a.txt", "b.txt", "c.txt"]})
        client.tool("set_hidden_files", pane=0, shown=False)
        s.expect("hidden files hidden", {"panes.0.hiddenFiles": False, "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"]})

        client.tool("set_filter_text", pane=0, text="b.")
        s.expect("name filter set", {"panes.0.filter": {"text": "b.", "barVisible": True}, "panes.0.pluginRows": ["b.txt"],
                                     "focus.role": "filter-bar"})
        client.tool("clear_filter", pane=0)
        s.expect("name filter cleared", {"panes.0.filter": {"text": "", "barVisible": False},
                                         "panes.0.pluginRows": ["sub", "a.txt", "b.txt", "c.txt"],
                                         "focus.role": "plugin:whale_testlist"})

        client.tool("set_split_view", enabled=True)
        s.expect("split view: test list left, Dolphin right", {
            "paneCount": 2, "activePane": 1, "panes.0.viewId": "whale_testlist", "panes.1.viewId": "dolphin",
            "panes.1.loading": False, "focus.role": "dolphin-item-view", "focus.pane": 1})

        client.tool("activate_pane", pane="left")
        s.expect("left pane activated", {"activePane": 0, "panes.0.active": True, "panes.1.active": False,
                                         "focus.role": "plugin:whale_testlist", "focus.pane": 0})
        client.tool("trigger_action", action="focus_other_view")
        s.expect("focus other view: Dolphin", {"activePane": 1, "focus.role": "dolphin-item-view", "focus.pane": 1})
        client.tool("trigger_action", action="focus_other_view")
        s.expect("focus other view: test list", {"activePane": 0, "focus.role": "plugin:whale_testlist", "focus.pane": 0})

        before = client.state()["activations"]["count"]
        client.tool("set_view", pane=1, view="whale_testlist")
        state = s.expect("test list on both sides, the inactive side stays inactive", {
            "panes.1.viewId": "whale_testlist", "activePane": 0, "focus.pane": 0})
        if state["activations"]["count"] != before:
            raise Failure(f"switching the inactive pane activated it: {state['activations']}")
        client.tool("set_view", pane=1, view="dolphin")
        s.expect("right side back to Dolphin", {"panes.1.viewId": "dolphin", "panes.1.dolphinViewVisible": True,
                                                "panes.1.focusProxy": "dolphin-item-view"})

        client.tool("set_view", pane=0, view="dolphin")
        s.expect("left side: test list -> Dolphin", {"panes.0.viewId": "dolphin", "panes.0.dolphinViewVisible": True,
                                                    "focus.role": "dolphin-item-view", "focus.pane": 0})
        client.tool("set_view", pane=0, view="whale_testlist")
        s.expect("left side: Dolphin -> test list", {"panes.0.viewId": "whale_testlist", "focus.role": "plugin:whale_testlist",
                                                    "focus.pane": 0})

        client.tool("set_split_view", enabled=False)
        s.expect("split view closed", {"paneCount": 1})
        image = client.tool("screenshot")[0]
        if image["type"] != "image" or len(image["data"]) < 1000:
            raise Failure("no screenshot")
        print("ok    screenshot")
    finally:
        client.close()
        for process in (whale, plain):
            if process is None:
                continue
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--whale", required=True)
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--plugin-dir", required=True)
    parser.add_argument("--platform", default="offscreen", help="empty for the session's platform")
    parser.add_argument("--screenshots", help="directory for a screenshot after every step")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        print("skipped: no D-Bus session bus")
        return 77
    if args.screenshots:
        os.makedirs(args.screenshots, exist_ok=True)

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-m3-"))
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
    print("M3 acceptance passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
