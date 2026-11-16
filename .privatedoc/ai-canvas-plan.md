# AI canvas and tools (CV0–CV5)

Branch `ai-canvas`, planned 2026-11-16 (discussion with the user). Status: plan only.

## Problem

Since AI9 the agent can read and operate Orcinus, but it cannot *show* anything (an image it made,
a sound it found, a before/after comparison) and it cannot *leave* anything behind: a small tool
that the user asked for ("let me sort these sounds") is gone with the conversation, or it ends up
as a script in a terminal or an add-on of some other program, cut off from the file manager.

## Goal

A generic **canvas** panel on which the agent presents its work, and on which it builds small
**tools** that the user can keep. Together with the file view and the chat this gives a
triptych: canvas left, files in the middle, chat right. In effect a small IDE focused on file
management, in which the AI extends Orcinus for the user.

Two example prompts that the design has to carry (from the discussion, both become agent test
scenarios):

- **Sound triage (UI only).** "Collect the sounds in the current folder and its subfolders,
  sorted by name, play the first one, show name and folder of the current sound, a folder picker
  'Target', buttons Prev / Next / Repeat / Keep (Keep copies the sound to Target and goes to the
  next one)." Needs no backend: QML plus the `orcinus` API.
- **Blender character randomizer (UI + backend).** "Characters are in /…/my_characters, morph
  targets in the subfolder morph, materials in …; a button Shuffle randomizes the character with a
  preview in the tool, a button Use saves the result as a Blender scene in the subfolder done."
  Needs a long-running backend (`blender -b --python backend.py`) that keeps the scene loaded,
  renders a small preview and saves on Use.

Not a goal: remote control of other programs (clicking in the canvas to work in Blender). Whoever
wants to work by hand opens the program with its full UI; the agent can also drive other programs
without Orcinus. Buttons that send commands to a backend ("Shuffle", "Render again") are fine.

## Decisions

- **A panel, not a window.** The canvas is a `Panel` subclass (dock `aiCanvasDock`) with its own
  tab bar: one tab "Canvas" for what the agent presents in the current conversation, plus one tab
  per opened or pinned tool. The triptych is a shipped profile "AI Workbench" (canvas left, view
  middle, chat right), not a new kind of window. Off by default, entry in the Panels menu, no
  toolbar button, no context menu entries (Dolphin's non-intrusive rule).
- **No embedding of foreign windows.** Wayland does not allow it. The canvas shows *output*
  (images, streams, files) of other programs, never their windows.
- **Tool UI is QML, not PyQt.** QML loads at runtime (`QQuickWidget` in the dock), agents write it
  well enough, and it runs in our process without a Python runtime, Qt/PyQt ABI matching or extra
  packages. PyQt in process was considered and dropped (2026-11-16): a crash of AI-written code
  would take down the file manager, and with backends (below) it is not needed.
- **Files through Orcinus.** A QML tool gets an `orcinus` object; reading, listing, copying,
  moving, renaming and trashing go through KIO, so changes have undo, deleting goes to the trash,
  remote locations (sftp, smb, …) work, and the agent's change log (AI9e) records them. QML on its
  own can read (images via `file://`, `FolderListModel`) but has no way to write, move or delete
  files. This is a **guard rail, not a sandbox**: we keep tools on the KIO path by design and by
  restricting imports (CV2), we do not promise isolation.
- **Backends are ordinary processes.** A tool may declare a backend command in its manifest
  (Blender, `python3 backend.py`, `ffmpeg`, anything). Orcinus starts it when the tool opens,
  stops it when the tool closes, and connects QML and backend with newline-delimited JSON over
  stdin/stdout (like ACP). A backend runs with the user's rights and writes where it wants, like
  the agent's shell today; the manifest *declares* its write targets and the UI shows them, but
  Orcinus does not enforce them. Say so honestly (tooltip, trust dialog, docs).
- **Tools are personal.** They live in `~/.local/share/orcinus/ai/tools/<id>/` (XDG data dir, not
  hidden in the home folder). The agent's work folder for a conversation is
  `~/.local/share/orcinus/ai/scratch/<conversation>/`. Nothing is shipped in the first round
  except, later and copied by hand, the sound triage tool as an example (it depends on nothing).
  Sharing tools between machines or people is not a goal for now.
- **Trust step on pinning.** A pinned tool starts again later without the agent watching, so
  pinning shows what the tool is (name, files) and what it runs (backend command, declared write
  targets) and asks once. Changing a pinned tool's files (by the agent or by hand) asks again
  before the next start.
- **Agent access through the existing MCP back channel** (`orcinus-mcp`, AI6), new tools with the
  same policy as AI9 (read-only mode: present and read yes, starting a backend or pinning no).
- **Code** in `src/whale/ai/canvas/`, part of `whaleai`; CMake option `WHALE_ENABLE_AI_CANVAS`
  (default ON when Qt Quick is found; the feature itself is off at runtime).

## Steps

**CV0 – Spike.** Throw-away, in the scratchpad or on the branch behind a dev flag:
- `QQuickWidget` in a dock inside the main window: focus handling between view, chat and canvas;
  key shortcuts of a QML tool must not fire the window's actions (and the reverse).
- mpv in QML: audio only (no video output, simple) and video (mpv render API in a
  `QQuickFramebufferObject` / `QQuickRhiItem`; the viewer's `WhaleVideoWidget` uses a QWidget
  today). Decide whether the QML player shares code with the viewer.
- QML restrictions: limited import path (only `QtQuick`, `QtQuick.Controls`, `QtQuick.Layouts`
  and our `Orcinus` module; no `Qt.labs.folderlistmodel`, no `QtQuick.LocalStorage`), an URL
  interceptor, XHR file access stays off (Qt 6 default). Check what remains possible and note it.
- One crude tool end to end: QML with a button that calls a C++ `orcinus.copy()`.
Result: notes in this plan, decisions for CV1–CV4.

**CV1 – Canvas panel and presenting.** Panel with tab bar and the "Canvas" tab; MCP tool
`canvas_show` with typed blocks, no code execution: image, video, audio, text/Markdown, file list
(clickable, reveals the item in the view), before/after comparison, folder watch (shows the newest
image of a folder, for renders and generated images) and stream (URL played by mpv: http, rtsp,
udp). `canvas_clear`; the canvas belongs to the conversation (cleared on "New Conversation").
Profile "AI Workbench" in `whaledefaultstyles.cpp`. Tests: blocks rendered offscreen, MCP tool
round trip, profile applies.

**CV2 – QML tools.** Tool folder with manifest `tool.json` (id, name, icon, entry QML, optional
backend, declared write targets, settings with defaults), tool tabs, open / close / pin, trust
dialog. The `Orcinus` QML module:
- context: `currentFolder()`, `selection()`, `otherFolder()` (other split view), signals when they
  change; a tool decides itself whether it follows the view or keeps its start folder (the agent
  must know both exist).
- reading: `list(url, {recursive, mime})` asynchronous with progress and cancel, `item(url)`
  (name, folder, size, MIME, dates), `readText(url)` (size limit).
- changing (KIO, undo, change log): `copy`, `move`, `rename`, `trash`, `mkdir`, `writeText`.
- view: `reveal(url)` (select in the view), `open(url)` (go to folder).
- UI: `pickFolder()`, `pickFiles()`, `player` (mpv: play, pause, seek, repeat, position), notify.
- `settings`: per tool, persistent (e.g. Target, last position).
Tests: API unit tests with a fake view; the sound triage tool written by hand as a fixture.

**CV3 – Development loop for the agent.** MCP tools: `tool_create` / write files into the tool
folder (or the agent writes them itself; the tool folder is reachable in its cwd list),
`tool_open`, `tool_reload`, `tool_log` (QML load and runtime errors, backend stderr), `tool_invoke`
(press a button / call a function exposed by the tool), `tool_screenshot` (image of the tool tab),
`tool_list`. A short "how to write an Orcinus tool" text that the system prompt (AI7) points to,
with the API and the manifest format. Agent test scenarios (AI10 runner): present an image, sound
triage from the example prompt (the agent writes it, reloads until it loads without errors,
invokes Next / Keep and checks the copy).

**CV4 – Backends.** Process lifecycle (start on open, stop on close and on Orcinus quit, restart
button, crash shown in the tab with stderr), JSON channel (`orcinus.backend.send()`, signal
`message`), backend working folder = tool folder, the tool's scratch folder in the environment;
declared write targets shown in the trust dialog and the tool's header. Test scenario: Blender
character randomizer (with a small test asset set; only where Blender is installed), plus a
Python backend test without Blender for CI.

**CV5 – Frame channel (optional).** Low-latency live preview: a local socket (per tool) over
which a backend sends frames (encoded, or raw through shared memory / memfd), shown by a
`LiveImage` item. Example: a Blender viewport rendered offscreen (`gpu.offscreen`), ComfyUI's
sampling previews. Only if CV1's folder watch and mpv streams are not enough in daily use.

Later, not planned: capturing a foreign window through the ScreenCast portal (with consent
dialog, like OBS); shipping more example tools; sharing tools.

## Open

- Video in QML (CV0) may be more work than audio; if so, CV1 starts with images, audio and text,
  and video follows.
- Whether the agent may *write* tool files directly (cwd list / shell) or only through
  `tool_create`; the latter lets Orcinus see every change (trust step for pinned tools).
- Documentation goes into AI8 together with the rest of the AI support.
