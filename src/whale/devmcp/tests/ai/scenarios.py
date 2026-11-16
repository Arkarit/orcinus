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


def agent_window(state):
    return state.get("agentWindow", {})


def panel(state, panel_id):
    return next((p for p in agent_window(state).get("panels", []) if p["id"] == panel_id), {})


def check_viewer_next(state, transcript, folder):
    viewer = state.get("viewer", {})
    if not viewer.get("open"):
        return FAIL, "viewer not open"
    name = folder_name(viewer.get("url", "")) or viewer.get("name", "")
    if name == "Urlaub-2.png":
        return PASS, ""
    if name.startswith("Urlaub-") and name != "Urlaub-1.png":
        return ACCEPTABLE, f"shows {name}"
    return FAIL, f"viewer shows {name}"


def check_info_panel_width(state, transcript, folder):
    info = panel(state, "information")
    if not info.get("visible"):
        return FAIL, "information panel hidden"
    width = info.get("width", 0)
    if abs(width - 400) <= 40:
        return PASS, f"{width} px"
    if abs(width - 400) <= 100:
        return ACCEPTABLE, f"{width} px"
    return FAIL, f"{width} px"


def check_split_two_thirds(state, transcript, folder):
    split = agent_window(state).get("splitView", {})
    if not split.get("enabled"):
        return FAIL, "no split view"
    share = split.get("leftShare", 0)
    if abs(share - 0.67) <= 0.05:
        return PASS, f"left {share}"
    if abs(share - 0.67) <= 0.12:
        return ACCEPTABLE, f"left {share}"
    return FAIL, f"left {share}"


def check_message_dismissed(state, transcript, folder):
    if agent_window(state).get("messages"):
        return FAIL, "message still shown"
    used = [c["ownTool"] for c in transcript["toolCalls"]]
    if "dismiss_message" in used:
        return PASS, ""
    return ACCEPTABLE, "message gone without dismiss_message (e.g. another folder opened)"


def check_new_tab(state, transcript, folder):
    tabs = state.get("tabs", {})
    url = state.get("url", "")
    if tabs.get("count") != 2:
        return FAIL, f"{tabs.get('count')} tabs"
    if folder_name(url) != "Dokumente":
        return FAIL, f"current tab shows {url}"
    return PASS, ""


def check_biggest_file(state, transcript, folder):
    answers = [e["text"] for e in transcript["entries"] if e["kind"] == "agent"]
    answer = answers[-1] if answers else ""
    if "Musik.mp3" in answer:
        return PASS, ""
    return FAIL, "Musik.mp3 not named"


def check_icons_type(state, transcript, folder):
    p = pane(state)
    if p["viewMode"] != "icons":
        return FAIL, f"view mode {p['viewMode']}"
    if p["sort"]["field"] != "type":
        return (ACCEPTABLE, "sorted by extension") if p["sort"]["field"] == "extension" else (FAIL, f"sorted by {p['sort']['field']}")
    return PASS, ""


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
    {
        "id": "viewer-next",
        "tags": ["host"],
        "setup": [{"tool": "set_location", "arguments": {"url": "{folder}/Bilder"}}],
        "prompt": "Zeig mir Urlaub-1.png im Viewer und blättere dann zum nächsten Bild.",
        "check": check_viewer_next,
        "rubric": "Opens the viewer on Urlaub-1.png, goes to the next image (Urlaub-2.png by name).",
    },
    {
        "id": "info-panel-width",
        "tags": ["host"],
        "prompt": "Blende das Informationspanel ein und mach es ungefähr 400 Pixel breit.",
        "check": check_info_panel_width,
        "rubric": "Shows Information (Panels ▸ Information), then set_panel_width about 400.",
    },
    {
        "id": "split-two-thirds",
        "tags": ["host"],
        "prompt": "Teile die Ansicht und gib der linken Seite etwa zwei Drittel der Breite.",
        "check": check_split_two_thirds,
        "rubric": "Split view on, set_split_ratio about 0.67.",
    },
    {
        "id": "dismiss-message",
        "tags": ["host"],
        "setup": [{"tool": "set_location", "arguments": {"url": "{folder}/gibtsnicht"}}],
        "prompt": "Mach die Fehlermeldung weg.",
        "check": check_message_dismissed,
        "rubric": "Closes the message with dismiss_message; may offer to go back to an existing folder.",
    },
    {
        "id": "new-tab",
        "tags": ["host"],
        "prompt": "Öffne den Ordner Dokumente in einem neuen Tab.",
        "check": check_new_tab,
        "rubric": "New tab, then opens Dokumente in it; the first tab keeps the play folder.",
    },
    {
        "id": "biggest-file",
        "tags": ["host"],
        "prompt": "Welche Datei ist hier die größte?",
        "check": check_biggest_file,
        "rubric": "Names Musik.mp3 (about 90 KB). A question: changing the view is not needed but harmless.",
    },
    {
        "id": "icons-type-en",
        "tags": ["host"],
        "prompt": "Switch to the icon view and sort by type.",
        "check": check_icons_type,
        "rubric": "English prompt, English answer. Icons + sorted by type.",
    },
]
