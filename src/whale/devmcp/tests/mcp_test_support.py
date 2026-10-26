#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Whale contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Shared helpers of the dev MCP acceptance tests: an MCP client over stdio, a step
runner that waits inside Whale, and starting a Whale in a profile of its own."""

import json
import os
import subprocess
import sys
import time

class Failure(Exception):
    pass


def portal_hint():
    """Returns a hint if the document portal of the session restarted lately, else "".

    Whale exports dragged and copied URLs to the document portal with a blocking D-Bus
    call (KUrlMimeData::exportUrlsToPortal()). When the portal keeps exiting, systemd
    stops starting it for a while, the call waits for its activation and Whale does not
    answer the automation in time ("NoReply")."""
    try:
        journal = subprocess.run(["journalctl", "--user", "--since", "-5min", "--no-pager", "-o", "cat",
                                  "-u", "xdg-document-portal.service"],
                                 capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return ""
    exits = journal.count("Main process exited")
    limited = "Start request repeated too quickly" in journal
    if not exits and not limited:
        return ""
    return (f" [hint: xdg-document-portal exited {exits} times in the last 5 minutes"
            + (" and systemd stopped restarting it" if limited else "")
            + "; Whale blocks while exporting URLs to it, so this is likely not a Whale bug."
            " See journalctl --user -u xdg-document-portal.service]")


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

    def tool(self, name, /, **arguments):  # a tool may have an argument "name"
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        if result["isError"]:
            text = result["content"][0]["text"]
            raise Failure(f"{name}{arguments}: {text}" + (portal_hint() if "NoReply" in text else ""))
        return result["content"]

    def tool_error(self, name, /, **arguments):
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
        # Without a window manager (offscreen) nothing activates the main window again
        # after a dialog closed; then only the logical focus (focus.role) can be checked.
        self.require_keyboard_focus = True

    def expect(self, title, conditions, timeout_ms=10000, focus=True):
        """Waits inside Whale until the conditions hold. No focus anomaly is ever allowed.
        With @p focus False (a popup menu or dialog is open), the keyboard focus is elsewhere."""
        self.step_count += 1
        conditions = dict(conditions)
        conditions.setdefault("focus.anomalies", [])
        if focus and self.require_keyboard_focus:
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


def install_test_opener(profile, log):
    """Makes text files open with a test "application" that only appends the file's
    path to @p log, so activating files never starts a real application."""
    applications = profile / "data" / "applications"
    applications.mkdir(parents=True, exist_ok=True)
    (applications / "whale-test-opener.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=Whale Test Opener\nMimeType=text/plain;\n"
        f"Exec=sh -c 'echo \"$1\" >> {log}' sh %f\n")
    (profile / "config").mkdir(parents=True, exist_ok=True)
    (profile / "config" / "mimeapps.list").write_text("[Default Applications]\ntext/plain=whale-test-opener.desktop\n")


def keep_single_view_at_startup(config):
    """New windows and tabs open with a single view, as before PI3, unless the test chose
    otherwise: by default they open with the split view that was open last."""
    orcinusrc = config / "orcinusrc"
    text = orcinusrc.read_text() if orcinusrc.exists() else ""
    if "SplitViewAtStartup=" in text:
        return
    entry = "SplitViewAtStartup=AlwaysSingle\n"
    if "[General]\n" in text:
        text = text.replace("[General]\n", "[General]\n" + entry, 1)
    else:
        text = "[General]\n" + entry + ("\n" + text if text else "")
    orcinusrc.write_text(text)


def start_whale(binary, folder, profile, plugin_dir, platform, automation, opener_log=None, file_operations=False,
                extra_plugin_dirs=(), confirm_delete=False, extra_args=()):
    """Starts Whale in @p profile (own XDG directories, so its trash is profile/data/Trash).
    Without @p folder Whale restores its session (if it is the only Whale on the bus).
    @p plugin_dir may be None: then Whale finds no plugins of the build tree. It may list
    several directories separated by os.pathsep (the product's view plugins and the test
    views Hello/Test List, which live in a test plugin directory of their own).
    With @p file_operations the dev MCP may change files and the clipboard, and deleting
    needs no confirmation (with @p confirm_delete it does, as KDE's default); only for
    throwaway test folders. @p extra_args go to Whale's command line before the folder."""
    env = dict(os.environ)
    env.pop("WHALE_DEV_MCP_FILE_OPERATIONS", None)
    for name in ("config", "data", "cache", "state"):
        (profile / name).mkdir(parents=True, exist_ok=True)
    keep_single_view_at_startup(profile / "config")
    if opener_log:
        install_test_opener(profile, opener_log)
    if file_operations:
        env["WHALE_DEV_MCP_FILE_OPERATIONS"] = "1"
        # KIO uses the home trash (XDG_DATA_HOME/Trash) only for files on the device of
        # $HOME; elsewhere it offers to delete permanently. A home in the profile keeps
        # trash and test files on one device and the trash inside the profile.
        (profile / "home").mkdir(exist_ok=True)
        env["HOME"] = str(profile / "home")
        (profile / "config" / "kiorc").write_text(
            f"[Confirmations]\nConfirmDelete={'true' if confirm_delete else 'false'}\nConfirmTrash=false\n")
    env.update({
        "XDG_CONFIG_HOME": str(profile / "config"),
        "XDG_DATA_HOME": str(profile / "data"),
        "XDG_CACHE_HOME": str(profile / "cache"),
        "XDG_STATE_HOME": str(profile / "state"),
        "QT_PLUGIN_PATH": os.pathsep.join([d for d in (plugin_dir, *extra_plugin_dirs) if d] + [env.get("QT_PLUGIN_PATH", "")]),
    })
    if platform:
        env["QT_QPA_PLATFORM"] = platform
    if automation:
        env["WHALE_DEV_MCP"] = "1"
    else:
        env.pop("WHALE_DEV_MCP", None)
    log = open(profile / "whale.log", "a")
    args = [binary, "--new-window", *extra_args] + ([str(folder)] if folder else [])
    return subprocess.Popen(args, env=env, stdout=log, stderr=subprocess.STDOUT)


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
