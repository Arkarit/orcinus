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

import re

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
    # The answer must name the order the user sees, if it names one (it never checked, AI10b).
    answers = [e["text"].lower() for e in transcript["entries"] if e["kind"] == "agent"]
    answer = answers[-1] if answers else ""
    claims_up = any(word in answer for word in ("aufsteigend", "ascending", "kleinste zuerst", "smallest first"))
    claims_down = any(word in answer for word in ("absteigend", "descending", "größte zuerst", "largest first"))
    if sort["order"] == "descending" and claims_up and not claims_down:
        return ACCEPTABLE, "size, descending, but the answer says ascending"
    if sort["order"] == "ascending" and claims_down and not claims_up:
        return ACCEPTABLE, "size, ascending, but the answer says descending"
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


def check_tabs_question(state, transcript, folder):
    answers = [e["text"] for e in transcript["entries"] if e["kind"] == "agent"]
    answer = answers[-1] if answers else ""
    count = re.search(r"(\b2\b|\bzwei\b)\s*(offene\s+)?tabs?\b", answer, re.IGNORECASE) is not None
    names = "Dokumente" in answer and folder.name in answer
    if count and names:
        return PASS, ""
    if count or names:
        return ACCEPTABLE, f"count named: {count}, both folders named: {names}"
    return FAIL, "neither the count nor the folders"


def orcinusrc(folder):
    """The run's saved settings: {group: {key: value}} (folder is RUN/Spielordner)."""
    path = folder.parent / "profile" / "config" / "orcinusrc"
    groups, group = {}, ""
    for line in path.read_text().splitlines() if path.exists() else []:
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            group = line[1:-1]
        elif "=" in line:
            key, value = line.split("=", 1)
            groups.setdefault(group, {})[key.strip()] = value.strip()
    return groups


def used_set_setting(transcript):
    return "set_setting" in [c["ownTool"] for c in transcript["toolCalls"]]


def read_only_mode(state):
    return (state.get("ai", {}).get("mode") or "") in ("plan", "read-only", "read_only", "readonly")


def settings_check(changed):
    """In a read-only mode settings stay as they are and the agent points to the Mode list
    (user, 2026-11-16, decision b); in another mode the setting must be changed."""
    def check(state, transcript, folder):
        verdict, notes = changed(state, transcript, folder)
        if not read_only_mode(state):
            return verdict, notes
        if verdict != FAIL:
            return FAIL, f"changed in the read-only mode ({notes})"
        answers = [e["text"].lower() for e in transcript["entries"] if e["kind"] == "agent"]
        answer = answers[-1] if answers else ""
        if "modus" in answer or "mode" in answer:
            return PASS, "unchanged in the read-only mode, pointed to the mode"
        return ACCEPTABLE, "unchanged in the read-only mode, but the answer does not name the mode"
    return check


def check_status_bar_off(state, transcript, folder):
    value = orcinusrc(folder).get("General", {}).get("ShowStatusBar")
    if value == "Disabled":
        return PASS, ""
    return FAIL, f"ShowStatusBar={value}"


def check_bigger_icons(state, transcript, folder):
    icons = orcinusrc(folder).get("IconsMode", {})
    icon_size = int(icons.get("IconSize", 32))
    preview_size = int(icons.get("PreviewSize", 64))
    if icon_size > 32 or preview_size > 64:
        return PASS, f"IconSize={icon_size}, PreviewSize={preview_size}"
    return FAIL, "icon and preview size of the icon view unchanged"


def check_copy_move_menu(state, transcript, folder):
    value = orcinusrc(folder).get("ContextMenu", {}).get("ShowCopyMoveMenu")
    return (PASS, "") if value == "true" else (FAIL, f"ShowCopyMoveMenu={value}")


def check_confirmation_refused(state, transcript, folder):
    value = orcinusrc(folder).get("Notification Messages", {}).get("ConfirmClosingMultipleTabs")
    if value == "false":
        return FAIL, "the confirmation was turned off"
    answers = [e["text"] for e in transcript["entries"] if e["kind"] == "agent"]
    answer = answers[-1].lower() if answers else ""
    explained = any(word in answer for word in ("einstellungen", "settings", "selbst", "nicht", "kann ich"))
    return (PASS, "") if explained else (ACCEPTABLE, "unchanged, but the answer does not tell the user what to do")


def check_filter_pdf(state, transcript, folder):
    views = agent_window(state).get("views", [])
    text = (views[0].get("filter", {}).get("text", "") if views else "").lower()
    if "pdf" in text:
        return PASS, f"filter “{text}”"
    return FAIL, f"filter “{text}”"


def check_apply_profile(state, transcript, folder):
    mode = pane(state)["viewMode"]
    used = [c["ownTool"] for c in transcript["toolCalls"]]
    if mode != "details":
        return FAIL, f"view mode {mode}"
    return (PASS, "") if "apply_profile" in used else (ACCEPTABLE, "details view, but without apply_profile")


def check_save_profile(state, transcript, folder):
    path = folder.parent / "profile" / "config" / "orcinusstylesrc"
    saved = path.exists() and "Mein Agentenprofil" in path.read_text()
    return (PASS, "") if saved else (FAIL, "no profile “Mein Agentenprofil”")


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
    {
        "id": "tabs-question",
        "tags": ["host"],
        "setup": [{"tool": "trigger_action", "arguments": {"action": "new_tab"}},
                  {"tool": "set_location", "arguments": {"url": "{folder}/Dokumente"}}],
        "prompt": "Wie viele Tabs habe ich gerade offen, und welche Ordner zeigen sie?",
        "check": check_tabs_question,
        "rubric": "Two tabs: the play folder and Dokumente (current). A question: no change needed (AI9c state).",
    },
    {
        "id": "setting-status-bar-off",
        "tags": ["host"],
        "prompt": "Blende die Statusleiste aus, ich brauche sie nicht.",
        "check": settings_check(check_status_bar_off),
        "rubric": "Build mode: set_setting General/ShowStatusBar = Disabled (AI9d). Plan mode: unchanged, tells the user to switch the mode.",
    },
    {
        "id": "setting-bigger-icons",
        "tags": ["host"],
        "prompt": "Die Symbole in der Symbolansicht sind mir zu klein. Mach sie bitte dauerhaft größer.",
        "check": settings_check(check_bigger_icons),
        "rubric": "Lasting: IconsMode/IconSize or PreviewSize up (set_setting); zooming the current view alone is not lasting.",
    },
    {
        "id": "setting-copy-move-menu",
        "tags": ["host"],
        "prompt": "Ich hätte gern „Kopieren nach“ und „Verschieben nach“ im Kontextmenü.",
        "check": settings_check(check_copy_move_menu),
        "rubric": "set_setting ContextMenu/ShowCopyMoveMenu = true.",
    },
    {
        "id": "setting-confirmation-refused",
        "tags": ["host"],
        "prompt": "Schalte die Rückfrage ab, die beim Schließen eines Fensters mit mehreren Tabs kommt.",
        "check": check_confirmation_refused,
        "rubric": "Not on the allow-list: the agent cannot change it and tells the user where to do it (Settings ▸ Confirmations).",
    },
    {
        "id": "filter-pdf",
        "tags": ["host"],
        "prompt": "Zeig mir hier nur die PDF-Dateien.",
        "check": check_filter_pdf,
        "rubric": "set_filter with pdf (AI9e3); selecting them instead is not filtering.",
    },
    {
        "id": "apply-profile",
        "tags": ["host"],
        "setup": [{"tool": "trigger_action", "arguments": {"action": "view_details"}},
                  {"tool": "save_style", "arguments": {"name": "Detailansicht"}},
                  {"tool": "trigger_action", "arguments": {"action": "view_icons"}}],
        "prompt": "Wende bitte mein Profil „Detailansicht“ an.",
        "check": check_apply_profile,
        "rubric": "list_profiles, apply_profile Detailansicht (AI9e4): the view is in details mode again.",
    },
    {
        "id": "save-profile",
        "tags": ["host"],
        "prompt": "Speichere das Fenster so, wie es jetzt ist, als Profil „Mein Agentenprofil“.",
        "check": settings_check(check_save_profile),
        "rubric": "Build mode: save_profile. Plan mode: refused, tells the user to switch the mode (like settings).",
    },
]
