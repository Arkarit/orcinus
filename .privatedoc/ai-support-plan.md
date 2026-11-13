# AI support (AI0–AI7)

Branch `ai-support`, started 2026-11-12. Status: AI0–AI3 done (2026-11-12); AI5 and AI4 done (2026-11-13); AI5d done (2026-11-13), next AI6 (back channel).

## Problem

People work on files with AI agents in a terminal (Claude Code, Gemini CLI, Goose, …). That works,
but it is cut off from the file manager: paths are copied by hand from Orcinus to the CLI, and the
agent cannot show a result in the file manager ("here are the 12 images that are too small").

## Goal

A dockable panel with a chat interface to an AI agent. The agent knows where the user is
(current folder, selection, the other split view) and can act on the file manager (at least: go
to a folder). Example: "The images in this folder: crop them all to 4:3."

## Decisions

- **Orcinus is a client, not an agent.** It does not call LLM APIs and has no tool loop of its
  own. It talks to external agents through the **Agent Client Protocol** (ACP,
  agentclientprotocol.com): JSON-RPC 2.0 over stdio, the client starts the agent as a subprocess.
  Any agent that speaks ACP works, cloud or local (e.g. Goose or OpenCode with Ollama; Claude Code
  through the Claude Agent ACP adapter; Gemini CLI, Codex CLI, Qwen Code, Mistral Vibe, …). Login,
  billing, model choice and tools stay with the agent.
- **Back channel through MCP.** `session/new` passes a list of MCP servers to the agent. Orcinus
  passes its own one: a small stdio executable (`orcinus-mcp`, C++, QtCore + QtDBus) that the
  agent starts and that talks over D-Bus to the window that owns the panel (a per-session token
  in the arguments binds it to that window). stdio MCP servers are the baseline every ACP agent
  supports; HTTP is optional in ACP. It is separate from the dev MCP (`WHALE_ENABLE_DEV_MCP`, not
  installed, test tool): small, safe tool set, installed.
- **Context as ACP content.** The current folder is the session's `cwd` at start; each prompt
  carries the current state (folder, selection, other split view) as `resource_link` blocks plus
  a short text block. The panel shows what is sent as removable chips, nothing is sent silently.
- **Permissions.** ACP's `session/request_permission` is shown inline in the chat (allow once /
  always / reject). A **permission bar** in the panel switches the agent's session mode
  (`session/set_mode`, modes come from the agent, e.g. "ask" / "accept edits") plus Orcinus' own
  policy for its MCP tools. Details in a settings dialog later; we learn what is needed by using
  it.
- **Non-intrusive.** The panel is off by default, only an entry in the Panels menu and a shortcut,
  no context menu entries, no toolbar button by default. No agent is configured by default;
  nothing leaves the machine before the user sets one up.
- **Private code.** New code in `src/whale/ai/` (and `src/whale/ai/mcp/` for the bridge), panel as
  a `Panel` subclass like the terminal panel. CMake option `WHALE_ENABLE_AI` (default ON, the
  feature itself is off at runtime).

## Steps

**AI0 – Spike.** Check ACP against real agents before we build on it: a throw-away script client
in the scratchpad, with Claude Agent ACP, Gemini CLI and one local setup (Goose or OpenCode +
Ollama). Questions: which session modes they report, whether they connect a stdio MCP server
passed by the client, how they report tool calls and file changes, startup time, auth flow.
Result: a short note here; adjust the plan.

*AI0 result (2026-11-12).* Throw-away Python client plus a fake stdio MCP server (tools
`get_location`, `set_location`, `select_items`), one prompt with a `resource_link` to a test folder
("list the image sizes, show `out` in Orcinus, select the images that are not 4:3"), then a write
task in a manual mode. Agents:

| Agent | Command | Modes (`modes` / `configOptions` category `mode`) | Other config options |
| --- | --- | --- | --- |
| Claude Agent 0.106 | `claude-agent-acp` (npm `@agentclientprotocol/claude-agent-acp`) | default (Manual), acceptEdits, plan, auto, bypassPermissions; starts in **auto** | model |
| Codex 2.6 | `codex-acp` (npm `@agentclientprotocol/codex-acp`; `@zed-industries/codex-acp` is deprecated) | read-only, workspace-write, agent (Auto review), agent-full-access; starts in **agent** | collaboration mode, model, reasoning effort, fast mode |
| OpenCode 1.19 | `opencode acp` | build, plan (config options only) | model |

OpenCode ran with LM Studio (OpenAI-compatible, `http://127.0.0.1:1234/v1`) and `qwen3.8-27b`
(32k context): all three MCP tools called correctly, ~25 s per prompt. Findings:

- Startup is fast: `initialize` 0.3–0.6 s, `session/new` 0.6–1.3 s. No login flow needed when the
  CLI is logged in already. `authMethods` differ (Codex: API key / ChatGPT; OpenCode: "run
  `opencode auth login` in a terminal"); the AI2 test button has to show them.
- All three connect the stdio MCP server from `session/new` and call its tools. The server gets no
  defined working directory: absolute paths only. The tool names the agents report differ
  (`mcp__orcinus__set_location`, `mcp.orcinus.set_location`, `orcinus_set_location`); the panel
  should learn about our tool calls from `orcinus-mcp` itself, not by parsing names.
- Permission requests work as specified: title, `kind` (execute/edit), `locations`, `rawInput`,
  for edits a diff, options with `kind` allow_once / allow_always / reject_once.
- **Agents start in a permissive mode** (Claude auto, Codex auto review, OpenCode allows everything
  unless its config says `ask`). Orcinus has to set the mode explicitly after `session/new`
  (remembered per agent).
- **Modes are not strict.** Claude in Manual ran `cp` without asking, but asked for `rm` and a
  file write. Orcinus can only answer what it is asked; the UI must not promise more. For OpenCode
  the permission level lives in its own config, not in a mode.
- Updates seen: `agent_message_chunk`, `agent_thought_chunk`, `tool_call` / `tool_call_update`,
  `usage_update`, `available_commands_update` (slash commands), `session_info_update`,
  `config_option_update`, `current_mode_update`. Unknown kinds must be ignored.
- No agent used `fs/*` or `terminal/*` of the client (none advertised): keep them out of v1.
- Privacy: a Claude session brings the skills and connectors of the claude.ai account. OpenCode
  fetches a model catalog from the net and offers free *cloud* models next to the local ones. The
  panel shows the current model (config option `model`) so the user sees where prompts go.
- Context: the agent missed the empty `out` folder (`find -type f`); the context text should name
  the subfolders too.
- The recorded traffic contains account data (skill lists etc.): not committed; the AI1 fake
  agent replays synthetic traffic modelled on it.

**AI1 – ACP client core.** `AcpConnection` (QProcess, newline-delimited JSON-RPC, requests with
ids, notifications, errors, cancel), `AcpSession` (initialize, `session/new`, `session/prompt`,
`session/update` stream, `session/cancel`, `session/set_mode`, permission requests as signals).
Client capabilities: none of `fs/*` or `terminal/*` in the first version (the agent uses its own
tools). Unit tests against a fake agent (small script that replays recorded traffic from AI0).

*AI1 result.* `src/whale/ai/` (static library `whaleai`, Qt Core only, behind the CMake option
`WHALE_ENABLE_AI`, not linked into the application yet): `WhaleAcpConnection` (process, JSON-RPC
lines, timeouts, stderr tail), `WhaleAcpSession` (start, prompt, cancel, permissions, config
options with a `modes` fallback), `whaleacptypes.h`. `whaleacpsessiontest` runs against
`whaleacpfakeagent` (scenarios basic, modes, crash, garbage, oldprotocol, auth, hang). Checked by
hand against Claude Agent and Codex in their manual modes: mode switch, MCP back channel,
permission requests with paths, file write. In the manual modes the agents also ask before calling
*our* MCP tools; see AI5.

**AI2 – Agent configuration.** List of agents (name, command, arguments, environment), stored in
`orcinusrc`; settings page "AI Agents" with add/edit/remove and a test button (runs `initialize`
and shows name, version, auth state). Suggestions for known agents found in `PATH`.

Details: `orcinusrc` `[AI]` `Agents` (ids in order), `DefaultAgent`; one subgroup per agent with
`Name`, `Command` (one shell-quoted command line, split with `KShell`), `Environment`
(`NAME=value` list) and `StartMode` (for AI5). Presets for the agents checked in AI0: Claude
(`claude-agent-acp`), Codex (`codex-acp`), OpenCode (`opencode acp`); if the adapter is not in
`PATH` but `npx` is, the preset runs it through `npx -y <package>` and says that this downloads it.
The test starts the agent in an empty temporary folder, runs `initialize` and `session/new`
without MCP servers and without a prompt (costs nothing), shows name, version, modes, model and,
on failure, the login hints (`authMethods`) and the end of the agent's log. "Defaults" of the
dialog leaves the agents alone (user data, like profiles). The page exists only with
`WHALE_ENABLE_AI`.

*AI2 result.* `WhaleAiAgents` (orcinusrc, presets), `WhaleAiAgentTest` (in `whaleai`, no
widgets), `WhaleAiSettingsPage` and `WhaleAiAgentDialog` (in `dolphinstatic`, page "AI Agents"
after "Viewer"). Tests: `whaleaiagentstest`, `whaleaisettingspagetest`. The test button was
checked by hand against Claude Agent, Codex, OpenCode and the npx variant of Claude (which leaves
the adapter in the npm cache, `~/.npm/_npx`). The agents start in Auto (Claude) / Auto review
(Codex) / build (OpenCode): the result shows the current mode, choosing the start mode is AI5.

**AI3 – Panel.** "AI Assistant" panel: agent selector, transcript (user/agent messages as
Markdown via `QTextDocument`, streamed chunks, tool calls as compact collapsible rows with
status), input field (Enter sends, Shift+Enter new line), stop button, new session. One agent
process per panel, started on the first prompt, ended when the panel closes or the agent changes.

Details: dock "AI Assistant" (`aiDock`, right area, hidden at creation so that it stays hidden for
existing users whose saved window state does not know it), action `show_ai_panel` (F8, free in
Orcinus) in the Panels menu only. The transcript is a scroll area with one widget per entry
(user message, agent message as `QLabel` with `Qt::MarkdownText`, thought, tool call row,
permission request, notice), so streamed text only re-renders its own entry and AI5 can put
buttons into entries. A tool call ends the current agent message: text after it is a new entry,
so the order on screen is the order of events. The minimal inline permission request (one button
per option of the agent) is part of AI3, since without it an agent that asks would hang; AI5 adds
the bar, the start mode and answering Orcinus' own tools. Hiding the dock keeps the session (F8
twice must not lose the conversation); it ends with the window, on "New Conversation", or when
another agent is chosen. The current model and mode are shown under the agent selector (privacy:
the user sees where prompts go). Links in answers: http(s) open in the browser, local paths in
Orcinus (folder: go there; file: its folder with the file selected). Without agents the panel
shows "Set Up Agents…", which opens the settings on the "AI Agents" page.

*AI3 result.* `WhaleAiPanel`, `WhaleAiTranscript` (in `dolphinstatic`), dock in
`DolphinMainWindow::setupDockWidgets()`. Test `whaleaipaneltest` (fake agent, panel in the main
window). If the agent cannot be started at all, the prompt stays in the input. Checked offscreen
with the real Claude agent: tool calls, Markdown table, model/mode/context line. Claude in its
default Auto mode asked no permission at all, which makes AI5 the next important step.

**AI4 – Context.** Chips for folder / selection / other split view above the input, follow the
active view, each removable for the next prompt. Drag and drop of files into the input adds them.
Large selections are capped (count, with a "+N more" note).

Details (planned 2026-11-13):

- *Source.* The window hands the panel a context provider (`std::function` returning
  `WhaleAiContext`: folder of the active view, its selection, folder of the other split view if
  split; local items only, since the agent works on local paths). The panel asks it when the
  window's URL or selection changes, when the active view changes, when the input gets the
  focus, and again right before sending (the other view can change without a signal of the
  window; what goes out is always current and shown in the transcript).
- *Chips* in a wrapping row between transcript and input: "Folder: name", "Selection: name" /
  "Selection: N items", "Other view: name", and one chip per file dropped onto the input. Each
  has a remove button; the tooltip shows the paths. Removing a folder/selection/other-view chip
  holds for the next message only (it comes back after sending, or when its content changes);
  dropped files are used by the next message and then go.
- *What is sent.* Before the user's text, one text block (English, for the agent, not
  translated) like "Context from the file manager Orcinus: current folder /a (subfolders: x,
  y, …); selected in it: …; other view: /b; added by the user: …", plus one `resource_link` per
  folder/item. Caps: 50 selected or dropped items (then "… and N more", which are not sent),
  30 subfolder names (AI0: the agent missed an empty subfolder, so the text names them).
- *Transcript.* The user message entry gets a small line "With: Folder x, 3 selected items, …"
  so nothing goes out unseen.
- *Drag and drop.* Local files and folders dropped onto the input become chips (not text in the
  input); other drops behave as before.

*AI4 result (2026-11-13).* `WhaleAiContext` + `WhaleAiContextPrompt` (`text()`, `blocks()`,
`summary()`) in `whaleai`; `WhaleAiContextChips` (own small flow layout) in `dolphinstatic`.
The window sets the provider in `setupDockWidgets()` and refreshes the visible panel on
`selectionChanged` and `activeViewChanged`, and on URL changes of the inactive view (linked
views); a hidden panel refreshes when shown. Non-local folders count if they have a local path
(`KFileItem::localPath()`, e.g. desktop:/). Only known limit: closing the inactive split view
gives no signal, so its chip stays until the next refresh (input focus, send). Tests:
`whaleaicontexttest`, panel tests `contextChips`, `dropFiles`, selection chip in
`panelInMainWindow`. The fake agent now echoes the user's text and the context separately.
Checked offscreen as an image: chips wrap below the transcript.

**AI5 – Permissions.** Inline permission requests; permission bar with the agent's modes; remember
"always" per session only in the first version. Calls of Orcinus' own harmless MCP tools (show a
folder, select) should not need a click each time: answer those requests directly, recognised
reliably (not by the tool title, which differs per agent; e.g. `orcinus-mcp` announces the call). Settings dialog with details only after we have
tried it.

Details (planned 2026-11-13, AI5 before AI4):

- *AI5a – Start mode.* After `session/new` and before the first prompt, the panel sets the agent's
  `StartMode` (orcinusrc, exists since AI2) through `setConfigOption()` on the option of
  category "mode"; the prompt waits for the answer. `WhaleAcpSession` gets a completion signal
  for `setConfigOption()` (success or the agent's error), since today a failure only becomes a
  protocol warning. If the agent does not offer the stored mode or refuses it, the prompt is
  *not* sent: the transcript says so and the user picks a mode in the bar. Agents without a mode
  option (none of the three checked, but ACP allows it) start as they are, with a notice that
  Orcinus cannot choose how much they may do. Every agent starts in its most
  restrictive mode until the user picks another one (user, 2026-11-13: the signal is "*you* are
  responsible, not Orcinus, Claude or Codex"): without a stored `StartMode`, the panel takes the
  first mode whose id is one of the known read-only ones (`plan`, `read-only`, `read_only`,
  `readonly`; Claude `plan`, Codex `read-only`, OpenCode `plan`). If none matches, the first
  prompt is not sent until the user picks a mode in the bar. Loosening is always the user's own
  choice; the transcript notes each mode change. This applies to agents set up before AI5 too
  (they have no `StartMode`).
  *AI5a result (2026-11-13).* `WhaleAcpSession::configOptionSet()` (outcome of
  `setConfigOption()`), `WhaleAiAgents::setStartMode()` / `restrictiveMode()`, and in the panel a
  small state machine (start mode unknown / applying / set / blocked) before `sendPending()`.
  `WhaleAiPanel::chooseMode()` is the entry point for the AI5b bar: without a session it only
  stores the start mode; with one it switches, notes it, stores it and sends a waiting message.
  While the message waits for the agent to take the start mode, the send button stops (ends the
  conversation); while it waits for the user, it does not. Mode changes the agent makes itself
  (e.g. after a model change) are noted, not stored. The settings page keeps the stored start
  mode on Apply (it may have changed in the panel meanwhile). Fake agent: mode `plan`, scenarios
  `noreadonly`, `nomode`, `refusemode`.
- *AI5b – Permission bar.* A combo box with the agent's modes between session info and
  transcript (replaces "Mode: …" in the info line). Choosing a mode switches the running session
  and stores it as the agent's `StartMode` (the bar remembers the last choice per agent). Before
  the first prompt the bar shows the stored start mode; the agent's list of modes is cached per
  agent (`Modes`, ids and names from the last session) so it can be chosen before a session
  exists. Mode names and descriptions come from the agent (tooltip). No claims of our own about
  what a mode allows: the tooltip says that the agent decides when it asks (AI0: modes are not
  strict).
  *AI5b result (2026-11-13).* Combo box "Mode:" (`modes`) under the info line, which no longer
  names the mode. Known modes per agent in orcinusrc (`ModeIds`, `ModeNames`,
  `ModeDescriptions`, `WhaleAiAgents::knownModes()` / `setKnownModes()`), updated from each
  conversation; `setAgents()` keeps them. Before a conversation: the stored start mode, else the
  read-only one; with nothing known the box is disabled and says "Read-only mode" (or the stored
  id). While a message waits for the user: no current item, "Choose a mode…". The tooltip has
  the agent's description of the mode plus the caveat that the agent decides when it asks.
- *AI5c – Better requests.* The inline request shows the kind (read, edit, delete, execute, …)
  with an icon, the paths, and foldable details: the command for `execute`, the diff for edits
  (from `content`), otherwise `rawInput` as JSON. Keyboard: the first allow option gets the focus
  when the request arrives while the panel has focus. "Always" options are passed to the agent,
  which remembers them for its session; Orcinus stores nothing (forgotten with the session).
  A request that arrives while the dock is hidden (the user gave a task and closed the panel
  while the agent works) shows the dock again (an agent that waits unseen looks hung; user
  agreed 2026-11-13, "for now").
  *AI5c result (2026-11-13).* The request entry shows an icon and a line for the kind (read,
  change, delete, move, search, run a command, fetch, change its mode), the command of an
  `execute` call in a fixed font and always visible (`rawInput.command`, string or word list),
  the paths, and folded "Changes" (the `diff` items of `content`: path, `-` / `+` lines, at most
  200) or else "Details" (`rawInput` as JSON). The first allow option gets the focus when the
  request arrives while the focus is in the panel. A request while the panel is hidden emits
  `WhaleAiPanel::attentionRequested()`, and the window shows and raises the dock; an inactive
  window is flagged with `QApplication::alert()`. Fake agent prompts `askexecute`, `askdiff`.
- *Own MCP tools* need `orcinus-mcp`, which is AI6: answering their requests directly moves to
  AI6, where the bridge announces each call to the panel before the agent asks.

**AI5d – Folder access (sandbox).** Planned 2026-11-13 after a test: the user gave Codex (mode
"Read-only") a folder outside the current one to delete; Codex asked, but nothing *kept* it from
the folder. The modes are promises of the agent; it reads anywhere in every mode. By default the
agent must reach only the current folder and its subfolders, reading and writing; everything
else of the user's data is not there for it.

- *Enforced by the system, not the agent.* Orcinus starts the agent inside a bubblewrap
  (`bwrap`) sandbox: the system read-only (`/usr`, `/etc`, … — the agent must run), the agent's
  own settings and login (e.g. `~/.codex`, `~/.claude`, `~/.config/opencode`; the exact list per
  agent comes from step 1), the shared folders read-write at their real paths; the rest of
  `$HOME` and the data mounts (`/media`, `/mnt`, `/run/media`) are not visible. No root needed
  (checked in the VM).
- *Access setting* "Access:" next to "Mode:" in the permission bar, two levels: "This folder and
  subfolders" (default) and "All folders" (no sandbox, as before AI5d). "Only this folder" was
  dropped (user, 2026-11-13): file systems share whole trees, so folders the agent creates itself
  would escape it. Stored per agent like `StartMode`.
- *Folders added while the agent runs* (user: restarting the agent on each folder change would
  be too disruptive). Checked in the VM: an outer user + mount namespace (sees the host) holds a
  shared tmpfs as the agent's view of `$HOME`; `bwrap` runs inside it (capabilities dropped, else
  it refuses) and binds that tmpfs. A bind mount the outer namespace adds later shows up in the
  sandbox at once, read and write; unmounting removes it. A small helper in the outer namespace
  takes "add / remove folder" from Orcinus. Parent folders of a shared folder appear empty.
- *UI.* The panel lists the shared folders of the conversation. The folder chip of a folder
  outside them says so ("no access") with "Give Access"; never automatic ("*you* are
  responsible"). Dropping a file from outside onto the input asks whether to share its folder.
  Shares last for the conversation; a new one starts with the current folder only.
- *Second line:* permission requests whose paths are all outside the shared folders are answered
  "reject" by Orcinus, with a note in the transcript (paths in shell commands are not reliable,
  hence only a second line).
- *Without `bwrap`* (or when the sandbox fails) the agent does not start with "This folder and
  subfolders": a clear message, no silent fallback to "All folders".

Steps:

- *AI5d1 – Spike.* Claude Agent ACP, Codex ACP (its own `bwrap` sandbox nested in ours) and
  OpenCode inside the sandbox: what they need beyond their settings (npm/npx caches, node, git
  and SSH settings, `/tmp`, D-Bus, keyring for logins), network, startup time. Note here; adjust.

  *AI5d1 result (2026-11-13).* Throw-away ACP client plus two sandbox scripts in the scratchpad;
  prompt: list the folder, write a file, read a file outside, `git --version`, `curl`.
  - *All three agents run* in a `bwrap` sandbox (system read-only, private `/tmp` and PID
    namespace, `$HOME` a tmpfs) with only: `~/.local/bin`, `~/.local/lib` (npm global, read-only),
    `~/.local/share/claude` (read-only), `~/.claude`, `~/.claude.json`, `~/.codex`, OpenCode's
    `~/.config|.local/share|.local/state|.cache/opencode`, `~/.npm`, and the folder. Logins work
    from these files (no keyring, no D-Bus needed). Write in the folder, git and network work;
    the file outside "does not exist" for each of them. No startup delay worth noting.
  - *Escape found: the session D-Bus.* With `/run/user/<uid>` visible, `systemd-run --user
    --pipe cat <file outside>` read the file (the user's systemd runs it outside). Hiding
    `/run/user` closes it (also Wayland, PipeWire, the keyring); the agents need none of it. The
    AI6 back channel then needs its own socket or a filtering proxy (`xdg-dbus-proxy`), never the
    bus itself. Other ways out checked: `/proc/<pid>/root` of host processes is denied (other user
    namespace; a PID namespace hides them anyway); X11's abstract socket is reachable (shared
    network namespace, the agents need the network) but Xwayland refuses without the cookie.
  - *Codex' own sandbox fails inside `bwrap`* on Ubuntu: AppArmor's `bwrap-userns-restrict` puts
    every child of `bwrap` into `unpriv_bwrap` (no capabilities), so Codex' nested `bwrap`
    cannot create its namespaces. Codex then runs each command outside its sandbox after an
    approval (in mode "agent" its "Guardian" auto-approves) — still inside ours, but its own
    read-only and no-network limits are gone. With our own namespaces instead of `bwrap`
    (`unshare --user --mount`, mounts by hand, capabilities dropped before the agent) Codex'
    sandbox works inside (its `curl` was blocked again).
  - *But the VM is not a normal Ubuntu:* `/etc/sysctl.d/20-apparmor.conf` (live-ISO leftover)
    sets `kernel.apparmor_restrict_unprivileged_userns = 0`; Ubuntu's default is 1. With 1, an
    unconfined `unshare` gets a user namespace without capabilities, so our own namespaces
    (needed for adding folders live as well) need an AppArmor profile for the helper (installed
    with the package, like `bwrap`'s), children unconfined so nested `bwrap` keeps its own
    profile. Other distributions have no such restriction.
  - *Tried with Ubuntu's restriction on* (sysctl set to 1 for the test, profile loaded into the
    kernel only): a plain `unshare` fails (`uid_map: Operation not permitted`); a copy of
    `unshare` with the profile `profile … flags=(unconfined) { userns, }` (the pattern of
    Ubuntu's own profiles for Chrome etc.) builds the sandbox. Inside it all three agents ran the
    prompt as before, Codex' own sandbox included (its `curl` blocked again), the file outside
    did not exist, `systemd-run --user` found no bus, a fresh `/proc` showed 3 processes.
  - *Adding folders live works under the restriction too,* with two levels: an outer holder
    process (user + mount namespace, sees the host; a shared tmpfs as the view of `$HOME`) and an
    inner mount namespace for the agent (`--propagation slave`, PID namespace, view bound over
    `$HOME`, `/media`, `/mnt`, `/run/user`, `/tmp` as tmpfs, capabilities dropped). `nsenter`
    into the holder's namespaces (no new namespace, so no AppArmor question) binds a host folder
    into the view: the agent sees it at once, reads and writes; `umount` takes it away (the
    empty mount point stays visible until removed).
  - *Decision for AI5d2:* our own small helper (`orcinus-sandbox`, C++ or C, installed next to
    `orcinus`) instead of `bwrap`: it builds both levels and takes "add / remove folder" from
    Orcinus over a pipe, so no `nsenter`. Packages and `global-install-orcinus.sh` install its
    AppArmor profile on Ubuntu; the dev build needs it loaded once by hand (documented), and
    without it the agent does not start (the message of AI5d).
  - *Open for AI5d2:* where agents live elsewhere (nvm, bun, `/opt`): bind the resolved package
    of the agent command read-only, plus a per-agent list of extra folders in the settings.
    `~/.gitconfig` read-only (commits need the identity); SSH keys not by default.
- *AI5d2 – Sandbox with fixed folders.* Start the agent in the sandbox with the current folder;
  "Access:" setting; the message without `bwrap`; the reject of requests outside.
  *AI5d2 result (2026-11-13).* `orcinus-sandbox` (`src/whale/ai/sandbox/`, plain C++, no Qt;
  installed to `libexec/orcinus`): own user + mount namespace that holds the agent's root as a
  shared tmpfs (system dirs read-only, fresh `/tmp`, `/run`, `/dev/shm`, `/var/tmp`, resolver
  dirs of `/run` read-only), then a slave mount + PID namespace for the agent (`pivot_root`,
  own `/proc`); `--bind` / `--ro-bind` (outer paths first, so a read-write folder inside a
  read-only one works), `--chdir`, `--check`; signals forwarded, exit status passed on, 125 for
  its own errors. AppArmor profile from `orcinus-sandbox.apparmor.in`: installed with
  `WHALE_INSTALL_APPARMOR_PROFILE` (off; `global-install-orcinus.sh` turns it on and loads it);
  the build tree gets `orcinus-sandbox.apparmor` for its own helper (`sudo apparmor_parser -r`,
  until the next reboot). `WhaleAiSandbox` (whaleai): `helper()` (next to the program, else the
  installed one), `check()`, `agentPaths()` (per kind: Claude `~/.claude`, `~/.claude.json`,
  `~/.local/share/claude`, `~/.cache/claude`; Codex `~/.codex`; OpenCode its XDG dirs; all:
  `~/.npm` read-write, `~/.local/bin`, `~/.local/lib`, git settings, the npm package of the
  agent command and an nvm-style node read-only; plus `SandboxPaths` in orcinusrc, no UI yet),
  `wrap()`, `reaches()`. `WhaleAiAgent::allFolders` (`Access=all`), `WhaleAiAgents::kind()`
  (shared with the login hints). Panel: "Access:" under "Mode:" ("This folder and subfolders" /
  "All folders"), stored per agent, a change during a conversation applies to the next one; the
  start notice says what the agent reaches; without a working sandbox the agent is not started
  (message, details with the helper's line and, in the build tree, the sudo command); requests
  whose paths are all outside are answered "reject" with a notice. Tried offscreen through the
  panel with the real Codex, Claude and OpenCode: all work, none read the file outside (Claude
  and OpenCode asked and Orcinus said no; Codex found nothing). Found on the way: the npm prefix
  (`~/.local`) must not be shared, only the package (it has the data of all programs).
  Open: the Ubuntu package (PPA: `dh_apparmor`, rules option) and the AUR recipe; a settings UI
  for extra paths; the fake agent's tests run the sandbox only where it may run (CI skips).
- *AI5d3 – Add and remove folders.* Helper in the outer namespace, list of shared folders, "Give
  Access" on the chip, the question on dropped files.
  *AI5d3 result (2026-11-13).* `orcinus-sandbox --control FD`: a socket with one line per
  change ("add PATH" / "remove PATH", answered "ok" / "error …"), served by a thread of the outer
  process; a removed path leaves no empty places behind. The root is now built on an empty
  holder folder (`$XDG_RUNTIME_DIR/orcinus-sandbox`, else `/tmp/.orcinus-sandbox-UID`) covered
  by a tmpfs in the helper's namespace only, so the host stays visible there and folders
  anywhere (also in `/tmp`) can be added later. `WhaleAiSandboxControl` (socketpair, its end kept
  across exec through `AgentCommand::childSetup` → `QProcess::setChildProcessModifier`).
  Panel: "The agent reaches: **start folder**, added (remove)" under Access; folder, other-view
  and added chips the agent does not reach say "(no access)" with a "Give Access" button (never
  automatic); notices for each change; the context text names what the sandbox reaches, so the
  agent does not try the rest. `WhaleAiSandbox::reaches()` tells the agent's own `/tmp` (for its
  requests) from a host path in `/tmp` (for the chips). A file dropped from outside gets the
  same button on its chip instead of a question. Tried with the fake agent through the real
  sandbox (read, write, take back); the real agents use the same mounts.

**AI6 – Back channel.** `orcinus-mcp` and the D-Bus interface on the window. First tools:
`get_location`, `set_location` (go to a folder, in the active view), `get_selection`,
`select_items` (in the current folder). Later candidates: open in a new tab / the other split view,
filter, show in the viewer. Each tool call that changes the view is visible in the transcript.

**AI7 – Documentation.** `docs/orcinus-vs-dolphin.md`, design notes, user docs (setup of a cloud
and a local agent).

## Open

- Session persistence (ACP `session/load`) across restarts: later.
- Several panels / windows: one session each; the MCP token keeps them apart.
- Whether to offer `fs/write_text_file` so writes go through KIO (trash, undo): after AI0, depends on
  how the agents use it.
- Agents that need a login in a terminal: the test button shows the hint; maybe "open in terminal
  panel" for the login.
