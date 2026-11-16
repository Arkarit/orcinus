# SPDX-FileCopyrightText: 2026 Orcinus contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""The scenarios of the agent tests (AI10, .privatedoc/ai-support-plan.md).

A scenario is a prompt, optional set-up through the dev MCP, and a check of the final state.
The check returns (verdict, notes): PASS (the expected state), ACCEPTABLE (a state that also
fulfils the request, e.g. the other sort order), FAIL. The runner adds UNCLEAR (timeout, agent
error) and downgrades PASS to ACCEPTABLE for deviations in behavior (see run_agent_tests.py).
`rubric` tells a human (or Claude, reading the report) how to judge the agent's answer.

Tags: `host` scenarios only change the window (safe on the host); `vm` scenarios may do
dangerous things and run only in the VM (user, 2026-11-16). There are none of them yet.
"""

PASS, ACCEPTABLE, FAIL, UNCLEAR = "PASS", "ACCEPTABLE", "FAIL", "UNCLEAR"


def pane(state, index=0):
    panes = state.get("panes", [])
    return panes[index] if index < len(panes) else {}


def folder_name(url):
    return url.rstrip("/").rsplit("/", 1)[-1]


def check_sort_size(state, transcript, folder):
    sort = pane(state)["sort"]
    if sort["field"] != "size":
        return FAIL, f"sorted by {sort['field']}"
    return PASS, f"size, {sort['order']}"


def check_hidden(state, transcript, folder):
    return (PASS, "") if pane(state)["hiddenFiles"] else (FAIL, "hidden files not shown")


def check_details(state, transcript, folder):
    mode = pane(state)["viewMode"]
    return (PASS, "") if mode == "details" else (FAIL, f"view mode {mode}")


def check_select_pngs(state, transcript, folder):
    p = pane(state, state.get("activePane", 0) or 0)
    if folder_name(p["url"]) != "Bilder":
        return FAIL, f"active view shows {p['url']}"
    expected = {f.name for f in (folder / "Bilder").iterdir() if f.suffix == ".png"}
    selected = set(p.get("selectedNames", []))
    if selected == expected:
        return PASS, ""
    if selected and selected <= expected:
        return ACCEPTABLE, f"only {sorted(selected)} of {sorted(expected)}"
    return FAIL, f"selected {sorted(selected)}, expected {sorted(expected)}"


def check_split_bilder(state, transcript, folder):
    if not state.get("splitView"):
        return FAIL, "no split view"
    right = pane(state, 1)
    notes = []
    if folder_name(right["url"]) != "Bilder":
        return FAIL, f"right view shows {right['url']}"
    verdict = PASS
    if right["viewMode"] != "icons":
        return FAIL, f"right view mode {right['viewMode']}"
    field = right["sort"]["field"]
    if field == "created":
        verdict = ACCEPTABLE
        notes.append("sorted by creation date (asked: date)")
    elif field != "modified":
        return FAIL, f"right view sorted by {field}"
    if folder_name(pane(state, 0)["url"]) != folder.name:
        verdict = ACCEPTABLE
        notes.append(f"left view moved to {pane(state, 0)['url']}")
    return verdict, "; ".join(notes)


SCENARIOS = [
    {
        "id": "sort-size",
        "tags": ["host"],
        "prompt": "Sortiere die Dateien hier nach Größe.",
        "check": check_sort_size,
        "rubric": "Short confirmation; no claim it did more than sorting.",
    },
    {
        "id": "hidden-on",
        "tags": ["host"],
        "prompt": "Zeig mir auch die versteckten Dateien.",
        "check": check_hidden,
        "rubric": "Short confirmation.",
    },
    {
        "id": "details-view",
        "tags": ["host"],
        "prompt": "Wechsle bitte in die Detailansicht.",
        "check": check_details,
        "rubric": "Short confirmation.",
    },
    {
        "id": "select-pngs",
        "tags": ["host"],
        "prompt": "Öffne den Ordner Bilder und markiere alle PNG-Dateien darin.",
        "check": check_select_pngs,
        "rubric": "Opens Bilder, selects exactly the .png files (not the .jpg); says what it selected.",
    },
    {
        "id": "split-bilder-icons-date",
        "tags": ["host"],
        "prompt": "Ich möchte rechts den Ordner Bilder sehen, in der Symbolansicht und nach Datum sortiert. Links soll der aktuelle Ordner bleiben.",
        "check": check_split_bilder,
        "rubric": "Split view on; right = Bilder, icons, by date; left unchanged. Several steps in one request.",
    },
]
