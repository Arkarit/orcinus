#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Real input on a Plasma/Wayland session for the acceptance tests, where synthesized
input is not enough (popup menus need the input serial of a real click or key).

It uses the session's own tools only: ydotool (a running ydotoold, a virtual input
device of the user) for pointer and keys, and a KWin script for the geometry of the test
window and the pointer position (its output comes back through the journal). The pointer
is moved in small steps until KWin reports it at the target, so pointer acceleration does
not matter. Callers must check through the dev MCP that the pointer is where they expect
(e.g. the treemap reports the cell as hovered) before they click.

Only for the test Whale's own window: it is found by its process ID, raised and kept above
other windows while the test runs.
"""

import os
import shutil
import subprocess
import time
import uuid

KEY_UP = 103
KEY_DOWN = 108
KEY_ENTER = 28
KEY_ESC = 1
KEY_COMPOSE = 127  # the Menu key
KEY_F2 = 60
KEY_DELETE = 111
KEY_LEFTCTRL = 29


class RealInputUnavailable(Exception):
    pass


class RealInput:
    def __init__(self, pid):
        if not shutil.which("ydotool") or not shutil.which("qdbus6") or not shutil.which("journalctl"):
            raise RealInputUnavailable("ydotool, qdbus6 or journalctl is missing")
        socket = os.environ.get("YDOTOOL_SOCKET") or f"/run/user/{os.getuid()}/.ydotool_socket"
        if not os.path.exists(socket):
            raise RealInputUnavailable(f"no ydotoold socket at {socket}")
        self.env = dict(os.environ, YDOTOOL_SOCKET=socket)
        # KWin is on the session's bus, also when the test runs on a private one.
        self.kwin_env = dict(os.environ)
        if os.environ.get("WHALE_SESSION_BUS_ADDRESS"):
            self.kwin_env["DBUS_SESSION_BUS_ADDRESS"] = os.environ["WHALE_SESSION_BUS_ADDRESS"]
        self.pid = pid
        self.client = None

    def _kwin(self, body):
        """Runs a KWin script; returns the lines it printed with print(TAG + ...)."""
        tag = "WHALEINPUT" + uuid.uuid4().hex[:8]
        name = tag.lower()
        path = f"/tmp/{name}.js"
        with open(path, "w") as f:
            f.write(body.replace("TAG", f'"{tag} "'))
        since = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time() - 1))
        try:
            script = subprocess.check_output(["qdbus6", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting.loadScript",
                                              path, name], text=True, env=self.kwin_env).strip()
            subprocess.check_call(["qdbus6", "org.kde.KWin", f"/Scripting/Script{script}", "org.kde.kwin.Script.run"],
                                  stdout=subprocess.DEVNULL, env=self.kwin_env)
            lines = []
            for _ in range(40):
                time.sleep(0.05)
                log = subprocess.run(["journalctl", "--user", "--since", since, "-o", "cat"], capture_output=True,
                                     text=True, env=self.kwin_env).stdout
                lines = [line.split(tag + " ", 1)[1] for line in log.splitlines() if tag + " " in line]
                if lines and lines[-1] == "end":
                    return lines[:-1]
            raise RealInputUnavailable("the KWin script did not answer")
        finally:
            subprocess.run(["qdbus6", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting.unloadScript", name],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=self.kwin_env)
            os.unlink(path)

    def prepare_window(self, keep_above=True):
        """Raises and activates the test window; returns its client area (x, y, width, height)."""
        lines = self._kwin(f"""
            for (const w of workspace.windowList()) {{
                if (w.pid == {self.pid} && w.normalWindow) {{
                    w.keepAbove = {'true' if keep_above else 'false'};
                    w.minimized = false;
                    workspace.activeWindow = w;
                    const g = w.clientGeometry;
                    print(TAG + g.x + " " + g.y + " " + g.width + " " + g.height);
                }}
            }}
            print(TAG + "end");
        """)
        if not lines:
            raise RealInputUnavailable(f"no window of process {self.pid}")
        x, y, width, height = (float(v) for v in lines[0].split())
        self.client = (x, y, width, height)
        return self.client

    def cursor(self):
        x, y = self._kwin('print(TAG + workspace.cursorPos.x + " " + workspace.cursorPos.y); print(TAG + "end");')[0].split()
        return float(x), float(y)

    def move_to(self, x, y):
        """Moves the real pointer to the screen position (x, y), always with some motion, so
        the window under it gets a motion event also if the pointer was there already."""
        cx, cy = self.cursor()
        if abs(x - cx) < 3 and abs(y - cy) < 3:
            subprocess.check_call(["ydotool", "mousemove", "-x", "6", "-y", "0"], env=self.env, stdout=subprocess.DEVNULL)
        for _ in range(40):
            cx, cy = self.cursor()
            dx, dy = round(x - cx), round(y - cy)
            if dx == 0 and dy == 0:
                return
            # Large steps are accelerated; halve them so the pointer approaches without overshooting far.
            if abs(dx) > 8 or abs(dy) > 8:
                dx, dy = int(dx / 2) or dx, int(dy / 2) or dy
            subprocess.check_call(["ydotool", "mousemove", "-x", str(dx), "-y", str(dy)], env=self.env,
                                  stdout=subprocess.DEVNULL)
        raise RealInputUnavailable(f"could not move the pointer to {x}, {y}")

    def move_to_window_position(self, wx, wy):
        # The window may have been moved or resized meanwhile.
        x, y, _, _ = self.prepare_window()
        self.move_to(x + wx, y + wy)

    def right_click(self):
        subprocess.check_call(["ydotool", "click", "0xC1"], env=self.env, stdout=subprocess.DEVNULL)

    def click(self):
        subprocess.check_call(["ydotool", "click", "0xC0"], env=self.env, stdout=subprocess.DEVNULL)

    def press(self):
        """Left button down (e.g. to start a drag)."""
        subprocess.check_call(["ydotool", "click", "0x40"], env=self.env, stdout=subprocess.DEVNULL)

    def release(self):
        subprocess.check_call(["ydotool", "click", "0x80"], env=self.env, stdout=subprocess.DEVNULL)

    def key_down(self, code):
        subprocess.check_call(["ydotool", "key", f"{code}:1"], env=self.env, stdout=subprocess.DEVNULL)

    def key_up(self, code):
        subprocess.check_call(["ydotool", "key", f"{code}:0"], env=self.env, stdout=subprocess.DEVNULL)

    def key(self, code, times=1):
        for _ in range(times):
            subprocess.check_call(["ydotool", "key", f"{code}:1", f"{code}:0"], env=self.env, stdout=subprocess.DEVNULL)
            time.sleep(0.05)

    def release_window(self):
        if self.client:
            self.prepare_window(keep_above=False)
