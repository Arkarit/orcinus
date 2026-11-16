#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""End-to-end check of the dev MCP's AI panel tools (AI10a, .privatedoc/ai-support-plan.md),
with the scripted agent whaleacpfakeagent instead of a real one (no model, no quota):

  A  a new conversation with the agent and start mode chosen; the panel shows
  B  a prompt goes through the panel; the agent works through the back channel (it turns on
     Show Hidden Files), the panel's action line and the turn's end are seen
  C  a permission request waits; it is answered through the transcript's button
  D  a new conversation starts the record afresh

Arguments as test_m4_acceptance.py, plus --fake-agent.
"""

import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the sources
from mcp_test_support import Failure, McpClient, start_whale, wait_for_instance


def configure_agent(config, fake_agent):
    """The fake agent as the only agent, the disclaimer accepted (the user's step)."""
    orcinusrc = config / "orcinusrc"
    text = orcinusrc.read_text() if orcinusrc.exists() else ""
    text += (f"\n[AI]\nAgents=fake\nDefaultAgent=fake\nDisclaimerAccepted=true\n"
             f"\n[AI][fake]\nName=Fake\nCommand={fake_agent} basic\n")
    orcinusrc.write_text(text)


def run(args, work):
    folder = work / "files"
    folder.mkdir(parents=True)
    (folder / "a.txt").write_text("a")
    (folder / ".hidden").write_text("h")
    profile = work / "profile"
    (profile / "config").mkdir(parents=True)
    configure_agent(profile / "config", args.fake_agent)

    whale = start_whale(args.whale, folder, profile, args.plugin_dir, args.platform, automation=True)
    client = McpClient(args.adapter, dict(os.environ, WHALE_DEV_MCP_PID=str(whale.pid)))
    try:
        client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                      "clientInfo": {"name": "test_ai_acceptance", "version": "1"}})
        client.notify("notifications/initialized")
        wait_for_instance(client, whale)
        tool_json = lambda name, **arguments: json.loads(client.tool(name, **arguments)[0]["text"])

        # A
        ai = client.state()["ai"]
        if not ai["available"] or ai["agents"] != ["fake"] or ai["sessionState"] != "none":
            raise Failure(f"A: unexpected start state {ai}")
        if client.tool_error("ai_new_conversation", agent="nobody") is None:
            raise Failure("A: an unknown agent was accepted")
        ai = tool_json("ai_new_conversation", agent="fake", mode="auto")["state"]
        if not ai["panelVisible"] or ai["conversation"] != 1:
            raise Failure(f"A: panel not shown or no new conversation: {ai}")
        print("ok    A: new conversation, panel shown")

        # B
        if client.state()["panes"][0]["hiddenFiles"]:
            raise Failure("B: hidden files shown before the prompt")
        sent = tool_json("ai_send_prompt", text="nopermission mcp:trigger_action:show_hidden_files")
        if not sent["state"]["busy"]:
            raise Failure(f"B: not busy after sending: {sent['state']}")
        ai = tool_json("ai_wait_idle", timeout_ms=30000)
        if ai["timedOut"] or ai["turnsFinished"] != 1 or ai["lastStopReason"] != "end_turn" or ai["mode"] != "auto":
            raise Failure(f"B: turn did not end as expected: {ai}")
        if not client.state()["panes"][0]["hiddenFiles"]:
            raise Failure("B: the agent's action did not reach the window")
        transcript = tool_json("ai_get_transcript")
        kinds = [entry["kind"] for entry in transcript["entries"]]
        texts = "\n".join(entry["text"] for entry in transcript["entries"])
        if "user" not in kinds or "agent" not in kinds or "action" not in kinds:
            raise Failure(f"B: transcript kinds {kinds}")
        if "Show Hidden Files" not in texts or "mcp=" not in texts:
            raise Failure(f"B: transcript text:\n{texts}")
        print(f"ok    B: prompt → back channel → window, turn ended ({ai['elapsedMs']} ms)")

        # C
        tool_json("ai_send_prompt", text="please ask")
        ai = tool_json("ai_wait_idle", timeout_ms=30000)
        if ai["timedOut"] or len(ai["pendingPermissions"]) != 1 or not ai["busy"]:
            raise Failure(f"C: no waiting permission request: {ai}")
        request = ai["pendingPermissions"][0]
        reject = next((o for o in request["options"] if o["kind"].startswith("reject")), None)
        if reject is None:
            raise Failure(f"C: no reject option: {request}")
        if client.tool_error("ai_answer_permission", key=request["key"], option="no-such-option") is None:
            raise Failure("C: an unknown option was accepted")
        tool_json("ai_answer_permission", key=request["key"], option=reject["id"])
        ai = tool_json("ai_wait_idle", timeout_ms=30000)
        if ai["timedOut"] or ai["pendingPermissions"] or ai["turnsFinished"] != 2:
            raise Failure(f"C: turn did not end after the answer: {ai}")
        transcript = tool_json("ai_get_transcript")
        answered = [p["answered"] for p in transcript["permissions"]]
        if len(answered) != 1 or reject["name"] not in answered[0]:
            raise Failure(f"C: answer not recorded: {transcript['permissions']}")
        print("ok    C: permission request answered through the transcript")

        # D
        ai = tool_json("ai_new_conversation")["state"]
        if ai["conversation"] != 2 or ai["turnsFinished"] != 0 or ai["sessionState"] != "none":
            raise Failure(f"D: record not reset: {ai}")
        transcript = tool_json("ai_get_transcript")
        if transcript["toolCalls"] or transcript["permissions"] or "user" in [e["kind"] for e in transcript["entries"]]:
            raise Failure(f"D: old conversation left over: {transcript}")
        print("ok    D: new conversation starts afresh")
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
    parser.add_argument("--fake-agent", required=True)
    parser.add_argument("--platform", default="offscreen", help="empty for the session's platform")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    args = parser.parse_args()
    if not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        print("skipped: no D-Bus session bus")
        return 77

    work = pathlib.Path(tempfile.mkdtemp(prefix="whale-ai-"))
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
    print("passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
