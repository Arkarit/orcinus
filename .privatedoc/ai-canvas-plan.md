# AI canvas and tools (CV0–CV5)

Branch `ai-canvas`, planned 2026-11-16 (discussion with the user). Status: CV0 (spike), CV1, CV2 and CV3 done 2026-11-16 (CV3's real-agent run pending).

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
- **Sound check (UI + backend).** "Go through the WAV files in the current folder, show each
  one's waveform, length, peak and loudness, mark clipped files and files with long silence at
  start or end; a button Trim writes a copy without the silence into the subfolder trimmed."
  Needs a long-running backend (`python3 backend.py`, standard library only: `wave`, `array`;
  no `audioop`, it is gone since Python 3.13) that keeps the decoded samples, sends waveform
  peaks and measurements over the JSON channel and writes the trimmed copy. Neutral, depends on
  nothing beyond Python, and runs in CI. (Replaced the earlier Blender character randomizer,
  2026-11-17: that needs Blender and test assets; it stays a possible later example.)

Not a goal: remote control of other programs (clicking in the canvas to work in Blender, say). Whoever
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

*CV0 results (done 2026-11-16).* Spike app in the session scratchpad (`cv0spike`: QMainWindow
with QActions, a `QQuickWidget` in a dock, the viewer's `WhaleVideoPlayer` compiled in). Offscreen
for keys, imports and KIO; video in a headless `kwin_wayland --virtual` (real OpenGL, no window on
the user's desktop). All checks passed:
- *Graphics:* `QQuickWidget` uses the OpenGL RHI on Wayland (software on "offscreen", as
  expected).
- *Video in QML works:* a `QQuickFramebufferObject` (`VideoOutput`) creates mpv's render context
  in `createFramebufferObject()` (GL context current there), renders with `FLIP_Y = 0`, update
  callback queued to `update()`; 24 frames in ~1 s of the test video, the grabbed framebuffer shows
  the picture. It drives the *same* `WhaleVideoPlayer` as the viewer, so the QML player reuses
  that class (`load`, pause, seek, position, loop); audio alone plays the same way. Offscreen (the
  tests) the player has no output and the item stays empty, like the viewer's tests.
  Caveat: the item needs the OpenGL RHI. If a user forces Vulkan (`QSG_RHI_BACKEND`), the video
  item shows nothing; the canvas then says so (audio still plays).
- *Keys:* with focus in the tool, a QML `Keys.onShortcutOverride` that accepts a key gets it, and
  the window's action with the same shortcut does *not* fire (K, Right). Keys the tool does not
  claim go to the window's actions: Delete and Ctrl+C fired the window's actions while the focus
  was in the tool, which in Orcinus would delete the view's selection. QML `Shortcut` items work
  inside the `QQuickWidget`. Focus back in the view, the window's shortcuts work again.
  *Decision for CV2:* the canvas widget accepts ShortcutOverride for every key without
  Ctrl/Alt/Meta (letters, digits, Space, Return, arrows, Delete, Backspace, …) while the focus is
  in a tool, so plain keys never reach the window's actions; Ctrl/Alt combinations and F-keys pass
  to the window unless the tool claims them.
- *Imports:* a `QQmlAbstractUrlInterceptor` sees every `qmldir` lookup and can redirect it to a
  missing file. With a deny list (`Qt/labs`, `QtQuick/LocalStorage`, `QtCore`, `QtMultimedia`,
  `QtNetwork`, `QtWebEngine`, `QtWebView`, `QtWebChannel`, `QtTest`, `Qt5Compat`) these imports
  fail with a load error, while `QtQuick`, `QtQuick.Controls` (+ Templates, impl, styles) and
  `QtQuick.Layouts` load. Controls pulls in style modules (Fusion/Basic here; on Plasma
  `org.kde.desktop`), so an allow list would have to follow styles: *decision: deny list*. A
  relative directory import (`import "../../etc"`) loads, but only gives QML files of that folder,
  no file access: harmless.
- *Write holes in plain QtQuick:* `item.grabToImage(cb)` + `result.saveToFile(path)` **overwrote
  an existing file** (PNG data) and cannot be intercepted; `Canvas.save()` wrote nothing here; XHR
  `PUT` to `file://` is refused (Qt 6 default, `QML_XHR_ALLOW_FILE_WRITE` unset). This confirms
  "guard rail, not sandbox": the trust dialog and the docs say that a tool can write files,
  the `orcinus` API is the *intended* way (undo, trash). Orcinus never sets
  `QML_XHR_ALLOW_FILE_WRITE`/`_READ`.
- *KIO from QML:* a `Button` whose `onClicked` calls a C++ `orcinus.copy(urls, dest)` (a
  `KIO::copy` recorded with `FileUndoManager::recordCopyJob`): a mouse click on the button ran the
  job, the file arrived, and undo was offered ("Undo: Copy"). In Orcinus the jobs go through the
  same helpers as AI9's file actions, so they also land in the agent's change log.

**CV1 – Canvas panel and presenting.** Panel with tab bar and the "Canvas" tab; MCP tool
`canvas_show` with typed blocks, no code execution: image, video, audio, text/Markdown, file list
(clickable, reveals the item in the view), before/after comparison, folder watch (shows the newest
image of a folder, for renders and generated images) and stream (URL played by mpv: http, rtsp,
udp). `canvas_clear`; the canvas belongs to the conversation (cleared on "New Conversation").
Profile "AI Workbench" in `whaledefaultstyles.cpp`. Tests: blocks rendered offscreen, MCP tool
round trip, profile applies.

*CV1 results (done 2026-11-16).* `src/whale/ai/canvas/`: `WhaleAiCanvasModel` (in `whaleai`:
blocks checked by `parse()`, all or nothing per call; max 50 blocks, 200 files, 64 KiB text;
watch = `KDirWatch` on the folder, newest image/video by mtime, a revision counter reloads a file
overwritten in place), `WhaleAiCanvasPanel` (dock `aiCanvasDock`, action `show_ai_canvas_panel`
without default shortcut, Panels menu, left area, hidden by default; tab bar with one tab for
now, auto-hidden), `Canvas.qml` (qrc `/orcinus/ai/canvas/`), `WhaleAiCanvasPlayer` +
`WhaleAiCanvasVideo` (QML module `org.kde.orcinus.canvas`, `VideoOutput`), `WhaleAiQuickWidget`,
`WhaleAiCanvasTools` (`canvas_show`, `canvas_clear`, `get_canvas`; allowed in every mode, a
hidden canvas is shown by `canvas_show`). Cleared by `WhaleAiPanel::conversationCleared`. Clicks
on files and local links select them in the view; other links only http(s). Nothing plays while
the canvas is hidden. Build: `WHALE_ENABLE_AI_CANVAS` → `HAVE_AI_CANVAS` (needs Qt Qml, Quick,
QuickWidgets, OpenGL); packaging gained qt6-declarative(-dev) and the qml6 QtQuick, Controls,
Layouts modules (Debian, Arch, CI container). Findings while building it:
- The ShortcutOverride of a `QQuickWidget` does not reach the widget: Qt sends it to the focused
  item of its Qt Quick window. `WhaleAiQuickWidget` therefore catches it with an application event
  filter (only for its own window/items while it has the focus), delivers it, and then keeps plain
  keys. Test: Delete in the canvas does not trigger a window action, F5 does.
- mpv's `vo=libmpv` fails to open a video when no render context exists yet: a video block now
  waits until its `VideoOutput` reports the render context (`videoReady`, fallback after 1.5 s:
  sound only). Audio loads at once. Verified in headless KWin: the test video plays in the canvas.
- `Image.sourceSize` scales small images *up*; not used. Images show at most at their own size.
- The list follows the end while images load (content height grows) until the user scrolls.
- Profiles: the "panels" element knows `ai` and `canvas` now (only where those docks exist); the
  shipped profile *AI Workbench* (Details, canvas + chat, no other panel) exists only for new
  users (profiles are seeded once). The default-setup e2e test takes `--ai-canvas`.
- Docs: only the shipped profile is mentioned (docs §5); the canvas itself goes into AI8.

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

*CV2 results (done 2026-11-16).* `WhaleAiToolStore` (`whaleai`: manifest, files, SHA-256
fingerprint over names and contents without hidden files, kept tools in orcinusrc `[AI Tools]`
`Pinned` + `Trusted_<id>`; a tool's own settings in `[AI Tool <id>]`, values as JSON),
`WhaleAiToolApi` (the `orcinus` object), `WhaleAiToolTab` (one tab per tool, a fresh
`WhaleAiQuickWidget`/engine per load, loaded when first shown), `WhaleAiQmlGuard` (deny list from
CV0). The canvas got a tab bar with a Tools button (tools of the folder, "Show Tools Folder") and
a tab context menu (Keep on Canvas, Reload, Show Folder, Close); kept tools have tabs again after
a restart, a kept tool whose fingerprint changed is blocked ("Keep Again…" / "Remove from Canvas").
Keeping asks with a list of the tool's files, what it runs (CV4) and where it says it writes.
Decisions and deviations from the plan above:
- API as built: `currentFolder()`, `selection()`, `otherFolder()`, signal `contextChanged`;
  `list(url, {recursive, mime, hidden, limit, progress}, cb)` + `cancel(id)`, `item(url, cb)`,
  `readText(url, cb, maxBytes)`; `copy`, `move`, `rename`, `trash`, `mkdir`,
  `writeText(url, text, overwrite, cb)`; `reveal(url)` (also covers "go to folder"; no separate
  `open`); `pickFolder(cb, start)`, `pickFiles(cb, {multiple, mime})`, `notify(text)`;
  `setting(key, fallback)` / `setSetting(key, value)`; `player` (a `WhaleAiCanvasPlayer` per
  tool, stopped when the tab is hidden); `toolName`, `toolFolder`. Callbacks: reads get
  `(value, error)`, changes `(ok, error)`; always called later, never synchronously.
- Undo: changes go into KIO's `FileUndoManager` (Orcinus' normal Undo), not the agent's change
  log: a tool's change is made by the user clicking in the tool. `writeText` never overwrites
  silently: with `overwrite` the old file goes to the trash first (undoable).
- Log: QML load errors, engine warnings and errors thrown in callbacks go into the tab's log
  (for CV3's `tool_log`). `console.log` is not captured yet (CV3).
- Not blocked: network access from QML (e.g. an `Image` from http) — note for CV3/docs.
- The example tool lives in `src/whale/ai/tests/data/tools/sound-triage/` as the test fixture
  (the user copies it into the tools folder by hand later). `whaleaitooltest` runs it end to end:
  sorted by name (natural), first one plays and is revealed, Keep (click) copies and goes on,
  Right / K / Left keys while the tool has the focus.

**CV3 – Development loop for the agent.** MCP tools: `tool_create` / write files into the tool
folder (or the agent writes them itself; the tool folder is reachable in its cwd list),
`tool_open`, `tool_reload`, `tool_log` (QML load and runtime errors, backend stderr), `tool_invoke`
(press a button / call a function exposed by the tool), `tool_screenshot` (image of the tool tab),
`tool_list`. A short "how to write an Orcinus tool" text that the system prompt (AI7) points to,
with the API and the manifest format. Agent test scenarios (AI10 runner): present an image, sound
triage from the example prompt (the agent writes it, reloads until it loads without errors,
invokes Next / Keep and checks the copy).

*CV3 results (done 2026-11-16).* `WhaleAiToolTools` (11 MCP tools) and `WhaleAiToolGuide`.
Decision (user, 2026-11-16): the agent writes tool files **only through `tool_write_file`**
(documented in the guide and the MCP instructions); a shell can still write them, which the
fingerprint catches for kept tools. As built:
- Reading (every mode): `tool_guide` (the how-to: files, QML rules, the API, the loop),
  `tool_list` (also folders with a broken manifest), `tool_read_file`, `tool_log`,
  `tool_inspect` (the root's declared properties + every item with an objectName: type,
  visible, enabled, text, checked, value, ...), `tool_screenshot` (PNG, at most 1024 px wide, as
  an MCP image content block: window tools may return `_mcpImage`, the server turns it into
  `{"type": "image"}` before the text), `tool_close`.
- Changing (refused in a read-only mode like `set_setting`, message "Canvas tools cannot be
  written, opened or used"): `tool_write_file` (text files .qml .js .mjs .json .md .txt .svg .py,
  256 KiB, 50 files, no hidden or outside paths; answers whether the manifest is valid),
  `tool_delete_file`, `tool_open` (opens, or loads again when open; there is no separate
  `tool_reload`), `tool_invoke` (click by objectName as a real mouse click, key sequences, or a
  root function with arguments).
- Writing into a *kept* tool through these tools un-keeps it (with a note for the agent and a
  transcript line), so the agent can iterate and the user keeps it again at the end; changes
  behind Orcinus' back still block a kept tool at the next start.
- The log is per load (cleared on load); `console.log` of the tool's files goes into it (a
  message handler that knows the tools' folders; engine warnings no longer go to stderr).
- Dev state `canvas` (blocks, open tools with log and inspect, tool ids) for the AI10 runner.
  Scenarios `canvas-image`, `canvas-compare` and `tool-sound-triage` (the user's prompt, plus "test
  it, copy nothing yet"; 30 min timeout; play folder has `Sounds/` with three short WAVs).
  Real-agent run: pending (needs LM Studio, ask the user first).

**CV4 – Backends.** Process lifecycle (start on open, stop on close and on Orcinus quit, restart
button, crash shown in the tab with stderr), JSON channel (`orcinus.backend.send()`, signal
`message`), backend working folder = tool folder, the tool's scratch folder in the environment;
declared write targets shown in the trust dialog and the tool's header. Test scenario: the sound
check tool (Python backend, standard library only; play folder with a few short 16-bit WAVs, one
clipped, one with silence at both ends): a hand-written fixture for the unit tests (lifecycle,
JSON channel, crash with stderr, restart) and an agent scenario `tool-sound-check` in which the
agent writes UI and backend from the prompt, runs it and trims one file. Runs everywhere, CI
included.

*CV4 design (2026-11-17).* Decision (user): a backend starts only after the user confirms it
once per version; kept tools the user trusts start directly. Reason: Orcinus answers its own MCP
tools itself, so an agent in build mode could otherwise run any code through `tool_open` without
the shell approval its agent asks for.
- *Manifest:* `"backend": {"command": ["python3", "backend.py"]}` – a non-empty list of strings,
  the program from `PATH` or a file in the tool's folder. Nothing else for now.
- *Process:* working folder = the tool's folder; environment plus `ORCINUS_TOOL_ID`,
  `ORCINUS_TOOL_FOLDER`, `ORCINUS_TOOL_SCRATCH` (`~/.local/share/orcinus/ai/scratch/tools/<id>/`,
  created) and `PYTHONUNBUFFERED=1`. Started when the tool loads (messages sent before it runs
  are queued), stopped when the tab closes, on reload (then started again), when the tool is
  blocked and when Orcinus quits: stdin closed, after 2 s terminate, after 1 s more kill. No
  automatic restart: a backend that ends shows a bar with its exit code, the last lines of stderr
  and Restart.
- *Channel:* newline-delimited JSON both ways. `orcinus.backend.send(value)`, signal
  `message(value)`, `state` (`none`, `waiting`, `running`, `exited`), `exitCode`, `restart()`.
  A stdout line that is not JSON and every stderr line go into the tab's log (`tool_log`). Lines
  longer than 8 MiB are dropped with a log line.
- *Confirmation:* a bar in the tab: "This tool runs a program: python3 backend.py. It runs with
  your rights and can change files." [Start] [Not Now]. The approval is stored per tool as a
  fingerprint of the backend command and of every file *except* `.qml` files (QML cannot start
  programs and only talks to the approved backend), so the agent's work on the UI does not ask
  again, a change of the backend does. The agent sees `waiting` in `tool_open`/`tool_inspect`.
- *Steps:* CV4a `WhaleAiToolBackend` (process, channel, limits, environment) + manifest check,
  unit tests with a Python fake backend (skipped without Python). CV4b approval, tab
  integration, QML API, bars. CV4c agent side: guide, log, inspect/dev state, dev MCP hook that
  confirms for the AI10 runner. CV4d the sound check tool as fixture (+ end-to-end test),
  scenario `tool-sound-check`, real-agent run.

**CV5 – Frame channel (optional).** Low-latency live preview: a local socket (per tool) over
which a backend sends frames (encoded, or raw through shared memory / memfd), shown by a
`LiveImage` item. Example: a Blender viewport rendered offscreen (`gpu.offscreen`), ComfyUI's
sampling previews. Only if CV1's folder watch and mpv streams are not enough in daily use.
*Status (2026-11-17):* deferred (user). CV0–CV4 merged into master; CV5 is picked up when daily use
needs a smooth live preview. Until then a backend can send small images base64-encoded over the
JSON channel (a few per second) or write into a watched folder.

Later, not planned: capturing a foreign window through the ScreenCast portal (with consent
dialog, like OBS); shipping more example tools; sharing tools.

## Open

- Video in QML (CV0) may be more work than audio; if so, CV1 starts with images, audio and text,
  and video follows.
- Whether the agent may *write* tool files directly (cwd list / shell) or only through
  `tool_create`; the latter lets Orcinus see every change (trust step for pinned tools).
- Documentation goes into AI8 together with the rest of the AI support.
