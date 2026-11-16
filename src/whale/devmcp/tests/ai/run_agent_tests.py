#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""Agent tests (AI10, .privatedoc/ai-support-plan.md): a real agent operates Orcinus.

For each run of a scenario (scenarios.py): a fresh offscreen Orcinus with its own profile and a
play folder, a new conversation in the AI panel, the prompt sent through the panel as the user
does, and when the agent is done, the final state and the transcript are judged. Not part of
ctest: it needs a real agent and a model.

The agent is OpenCode with a local model of LM Studio (user, 2026-11-16: spare the cloud
quota). OpenCode gets a config of its own in the run's profile: it can read, and use Orcinus'
back channel, but shell, edits and web access are denied there. Permission requests for
anything but Orcinus' own tools are rejected by the runner. The play folder is compared before
and after every run. The runner never starts LM Studio: its server must already serve.

Verdicts: PASS, ACCEPTABLE (also fulfils the request, or a deviation in behavior), FAIL,
UNCLEAR (timeout, agent error). Results go to OUT/report.json and OUT/report.md; each run keeps
its state, transcript and Orcinus log in OUT/runs/.

    run_agent_tests.py --out DIR [--scenario ID ...] [--repeat N] [--model ID]
"""

import argparse
import datetime
import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.request
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from mcp_test_support import Failure, McpClient, start_whale, wait_for_instance  # noqa: E402
from scenarios import ACCEPTABLE, FAIL, PASS, SCENARIOS, UNCLEAR  # noqa: E402

SOURCE = HERE.parents[4]
VM_HOSTNAME = "user-kubuntu-virtual"
# Sent once when the agent ends with a question instead of acting (e.g. a plan in plan mode).
FOLLOW_UP = "Ja, bitte führe es jetzt direkt aus. Wo es Wahlmöglichkeiten gibt, nimm die übliche Voreinstellung."
# What an agent may do on its own besides Orcinus' tools: look, never change.
HARMLESS_KINDS = {"read", "search", "think", "switch_mode", "other", ""}


def png(width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    row = b"\x00" + bytes(rgb) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))


def make_play_folder(folder):
    """The same small folder for every run: files of distinct sizes and dates, two subfolders."""
    folder.mkdir(parents=True)
    base = time.time() - 30 * 86400
    files = {
        "Notizen.txt": b"Einkaufen, Steuer, Urlaub\n",
        "Bericht.odt": b"x" * 40000,
        "Rechnung-2026-03.pdf": b"%PDF-1.4\n" + b"y" * 12000,
        "Musik.mp3": b"ID3" + b"z" * 90000,
        ".versteckt": b"hidden\n",
    }
    for index, (name, data) in enumerate(files.items()):
        path = folder / name
        path.write_bytes(data)
        os.utime(path, (base + index * 86400, base + index * 86400))
    (folder / "Bilder").mkdir()
    for index in range(4):
        path = folder / "Bilder" / f"Urlaub-{index + 1}.png"
        path.write_bytes(png(32 + 16 * index, 24, (60 * index, 120, 200)))
        os.utime(path, (base + (4 - index) * 86400, base + (4 - index) * 86400))
    (folder / "Bilder" / "Portrait.jpg").write_bytes(b"\xff\xd8\xff\xe0" + b"j" * 3000)
    (folder / "Dokumente").mkdir()
    (folder / "Dokumente" / "Lebenslauf.txt").write_text("Lebenslauf\n")


def snapshot(folder):
    """Names, sizes and modification times of everything in @p folder."""
    result = {}
    for path in sorted(folder.rglob("*")):
        info = path.lstat()
        result[str(path.relative_to(folder))] = [path.is_dir(), info.st_size, int(info.st_mtime)]
    return result


def lmstudio_models(base_url):
    """The models LM Studio serves, or None if its server does not answer (it is never started)."""
    try:
        with urllib.request.urlopen(base_url.rstrip("/") + "/models", timeout=3) as response:
            return [model["id"] for model in json.load(response).get("data", [])]
    except (OSError, ValueError):
        return None


def opencode_config(path, base_url, model):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({
        "$schema": "https://opencode.ai/config.json",
        "model": f"lmstudio/{model}",
        "provider": {"lmstudio": {
            "npm": "@ai-sdk/openai-compatible",
            "name": "LM Studio (local)",
            "options": {"baseURL": base_url},
            "models": {model: {"name": model}},
        }},
        # Look, never change: the tests run on the host.
        "permission": {"edit": "deny", "bash": "deny", "webfetch": "deny"},
        # No subagents either: without bash, a subagent asked to run `ls` hung until the timeout.
        "tools": {"write": False, "edit": False, "patch": False, "bash": False, "webfetch": False, "task": False},
        "autoupdate": False,
        "share": "disabled",
    }, indent=1))


def configure_profile(profile, agent_command, opencode_file):
    config = profile / "config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "orcinusrc").write_text(
        "[AI]\nAgents=opencode\nDefaultAgent=opencode\nDisclaimerAccepted=true\n\n"
        f"[AI][opencode]\nName=OpenCode\nCommand={agent_command}\n"
        f"Environment=OPENCODE_CONFIG={opencode_file}\n")


class Run:
    def __init__(self, args, scenario, number, directory):
        self.args = args
        self.scenario = scenario
        self.number = number
        self.directory = directory
        self.deviations = []
        self.call_deviations = []
        self.result = {"scenario": scenario["id"], "run": number, "prompt": scenario["prompt"]}

    def tool(self, name, **arguments):
        return json.loads(self.client.tool(name, **arguments)[0]["text"])

    def converse(self, text):
        """Sends @p text and waits for the end of the turn, answering permission requests."""
        sent = self.tool("ai_send_prompt", text=text)
        if not sent.get("ok", True):
            raise Failure(f"prompt not sent: {sent}")
        deadline = time.monotonic() + self.args.timeout
        while True:
            remaining = int((deadline - time.monotonic()) * 1000)
            if remaining <= 0:
                self.tool("ai_stop")
                return "timeout"
            ai = self.tool("ai_wait_idle", timeout_ms=min(remaining, 1800000))
            for request in ai.get("pendingPermissions", []):
                options = request["options"]
                if request.get("ownTool"):
                    choice = next((o for o in options if o["kind"] == "allow_once"), None)
                else:
                    choice = next((o for o in options if o["kind"].startswith("reject")), None)
                    self.deviations.append(f"asked for {request.get('kind') or '?'}: {request.get('title')} (rejected)")
                if choice is None:
                    self.tool("ai_stop")
                    return f"permission request without a fitting option: {request}"
                self.tool("ai_answer_permission", key=request["key"], option=choice["id"])
            if ai.get("pendingPermissions"):
                continue
            if ai.get("timedOut"):
                continue
            if not ai["busy"]:
                if ai["sessionState"] in ("failed", "none") and ai.get("error"):
                    return f"agent failed: {ai['error']}"
                return None

    def judge(self, state, transcript, before, after):
        # Recomputed after a follow-up: the calls of the whole conversation.
        self.call_deviations = [f"tool call {call['kind']}: {call['title']} ({call['status']})"
                                for call in transcript["toolCalls"]
                                if not call["ownTool"] and call["kind"] not in HARMLESS_KINDS]
        if before != after:
            changed = sorted(set(map(str, before.items())) ^ set(map(str, after.items())))
            self.call_deviations.append(f"play folder changed: {changed[:5]}")
            return FAIL, "the play folder changed"
        verdict, notes = self.scenario["check"](state, transcript, self.folder)
        if verdict == PASS and (self.deviations or self.call_deviations):
            verdict = ACCEPTABLE
        return verdict, notes

    def execute(self):
        self.folder = self.directory / "Spielordner"
        make_play_folder(self.folder)
        profile = self.directory / "profile"
        opencode_file = profile / "opencode.json"
        opencode_config(opencode_file, self.args.base_url, self.args.model)
        configure_profile(profile, self.args.agent_command, opencode_file)
        before = snapshot(self.folder)

        whale = start_whale(self.args.whale, self.folder, profile, self.args.plugin_dir, "offscreen", automation=True)
        self.client = McpClient(self.args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
        started = time.monotonic()
        try:
            self.client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                               "clientInfo": {"name": "run_agent_tests", "version": "1"}})
            self.client.notify("notifications/initialized")
            wait_for_instance(self.client, whale)
            self.tool("set_window_size", width=1400, height=900)
            for step in self.scenario.get("setup", []):
                arguments = {key: value.replace("{folder}", self.folder.as_uri()) if isinstance(value, str) else value
                             for key, value in step.get("arguments", {}).items()}
                self.tool(step["tool"], **arguments)
            if self.scenario.get("setup"):
                time.sleep(1)  # let the set-up settle (folder listing, message)
            self.tool("ai_new_conversation", agent="opencode", mode=self.args.mode)
            problem = self.converse(self.scenario["prompt"])
            follow_up = False
            while True:
                state = self.client.state()
                transcript = self.tool("ai_get_transcript")
                after = snapshot(self.folder)
                if problem:
                    verdict, notes = UNCLEAR, problem
                    break
                verdict, notes = self.judge(state, transcript, before, after)
                answers = [e["text"] for e in transcript["entries"] if e["kind"] == "agent"]
                asked = bool(answers) and "?" in answers[-1][-300:]
                if verdict != FAIL or follow_up or not asked or before != after or self.args.no_follow_up:
                    break
                # It asked instead of acting: answer once, as a user would.
                follow_up = True
                problem = self.converse(FOLLOW_UP)
            if follow_up:
                self.deviations.append("needed a follow-up (the agent asked instead of acting)")
                if verdict == PASS:
                    verdict = ACCEPTABLE
        finally:
            try:
                self.client.close()
            except Exception:
                pass
            whale.terminate()
            try:
                whale.wait(timeout=10)
            except Exception:
                whale.kill()
        answers = [e["text"] for e in transcript["entries"] if e["kind"] == "agent"]
        own = [c["ownTool"] for c in transcript["toolCalls"] if c["ownTool"]]
        self.result.update({
            "verdict": verdict,
            "notes": notes,
            "deviations": self.deviations + self.call_deviations,
            "seconds": round(time.monotonic() - started, 1),
            "ownTools": own,
            "toolCalls": len(transcript["toolCalls"]),
            "followUp": follow_up,
            "answer": answers[-1] if answers else "",
            "rubric": self.scenario.get("rubric", ""),
        })
        (self.directory / "state.json").write_text(json.dumps(state, indent=1))
        (self.directory / "transcript.json").write_text(json.dumps(transcript, indent=1, ensure_ascii=False))
        (self.directory / "result.json").write_text(json.dumps(self.result, indent=1, ensure_ascii=False))
        return self.result


def write_report(out, results, args):
    (out / "report.json").write_text(json.dumps({"model": args.model, "results": results}, indent=1, ensure_ascii=False))
    lines = [f"# Agent tests ({datetime.datetime.now():%Y-%m-%d %H:%M})", "",
             f"Agent: `{args.agent_command}`, model `{args.model}`, mode `{args.mode or 'start mode'}`", "",
             "| Scenario | PASS | ACCEPTABLE | FAIL | UNCLEAR | Ø s |", "|---|---|---|---|---|---|"]
    for scenario in dict.fromkeys(r["scenario"] for r in results):
        runs = [r for r in results if r["scenario"] == scenario]
        count = lambda v: sum(1 for r in runs if r["verdict"] == v)
        mean = sum(r["seconds"] for r in runs) / len(runs)
        lines.append(f"| {scenario} | {count(PASS)} | {count(ACCEPTABLE)} | {count(FAIL)} | {count(UNCLEAR)} | {mean:.0f} |")
    lines += ["", "## Runs", ""]
    for r in results:
        lines.append(f"### {r['scenario']} #{r['run']}: {r['verdict']}")
        lines.append(f"- Prompt: {r['prompt']}")
        if r["notes"]:
            lines.append(f"- Notes: {r['notes']}")
        for deviation in r["deviations"]:
            lines.append(f"- Deviation: {deviation}")
        lines.append(f"- Orcinus tools: {', '.join(r['ownTools']) or 'none'} ({r['toolCalls']} tool calls, {r['seconds']} s)")
        lines.append(f"- Rubric: {r['rubric']}")
        answer = r["answer"].strip().replace("\n", "\n  > ")
        lines.append(f"- Answer:\n  > {answer}" if answer else "- Answer: (none)")
        lines.append("")
    (out / "report.md").write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    build = SOURCE.parent / "build-whale"
    parser.add_argument("--whale", default=str(build / "bin" / "orcinus"))
    parser.add_argument("--adapter", default=str(SOURCE / "src/whale/devmcp/adapter/whale_dev_mcp.py"))
    parser.add_argument("--plugin-dir", default=str(build / "plugins"))
    parser.add_argument("--out", help="directory for the report and the runs")
    parser.add_argument("--scenario", action="append", help="scenario id (repeatable); default all host scenarios")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=600, help="seconds per run for the agent")
    parser.add_argument("--base-url", default="http://127.0.0.1:1234/v1", help="LM Studio's OpenAI-compatible API")
    parser.add_argument("--model", default="qwen3.8-27b")
    parser.add_argument("--mode", default="", help="the agent's mode; default Orcinus' start mode (read-only)")
    parser.add_argument("--agent-command", default=None, help="default: opencode acp, else npx -y opencode-ai acp")
    parser.add_argument("--no-follow-up", action="store_true", help="never answer an agent that asks back")
    parser.add_argument("--list", action="store_true", help="list the scenarios")
    parser.add_argument("--no-model-check", action="store_true",
                        help="skip asking LM Studio (for a dry run with --agent-command of a scripted agent)")
    args = parser.parse_args()

    if args.list:
        for scenario in SCENARIOS:
            print(f"{scenario['id']:28} {','.join(scenario['tags']):6} {scenario['prompt']}")
        return 0
    if not args.out:
        parser.error("--out is required")
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        print("no D-Bus session bus")
        return 2
    if args.agent_command is None:
        args.agent_command = "opencode acp" if shutil.which("opencode") else "npx -y opencode-ai acp"

    on_vm = socket.gethostname() == VM_HOSTNAME
    chosen = [s for s in SCENARIOS if not args.scenario or s["id"] in args.scenario]
    unknown = set(args.scenario or []) - {s["id"] for s in SCENARIOS}
    if unknown:
        print(f"unknown scenarios: {sorted(unknown)}")
        return 2
    for scenario in chosen:
        if "host" not in scenario["tags"] and not on_vm:
            print(f"refused: {scenario['id']} runs only in the VM")
            return 2

    models = None if args.no_model_check else lmstudio_models(args.base_url)
    if args.no_model_check:
        pass
    elif models is None:
        print(f"LM Studio does not answer at {args.base_url}; start its server first (the runner never does).")
        return 2
    elif args.model not in models:
        print(f"model {args.model!r} not served; LM Studio offers: {', '.join(models)}")
        return 2

    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for scenario in chosen:
        for number in range(1, args.repeat + 1):
            directory = out / "runs" / f"{scenario['id']}-{number}"
            if directory.exists():
                shutil.rmtree(directory)
            directory.mkdir(parents=True)
            print(f"{scenario['id']} #{number} ...", end=" ", flush=True)
            try:
                result = Run(args, scenario, number, directory).execute()
            except Failure as failure:
                result = {"scenario": scenario["id"], "run": number, "prompt": scenario["prompt"], "verdict": UNCLEAR,
                          "notes": f"runner: {failure}", "deviations": [], "seconds": 0, "ownTools": [], "toolCalls": 0,
                          "answer": "", "rubric": scenario.get("rubric", ""), "followUp": False}
            print(f"{result['verdict']} ({result['seconds']} s) {result['notes']}")
            results.append(result)
            write_report(out, results, args)
    print(f"report: {out / 'report.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
