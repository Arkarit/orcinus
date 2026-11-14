# AI support (AI0–AI7)

Branch `ai-support`, started 2026-11-12. Status: AI0–AI3 done (2026-11-12); AI5 and AI4 done (2026-11-13); AI5d done (2026-11-13); AI6 done (2026-11-14): back channel + attack tests passed. Next AI7 (documentation).

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
  passes its own one: a small stdio executable (`orcinus-mcp`) that the agent starts and that
  relays to the panel over a private Unix socket of the conversation (a per-conversation token
  binds it to that panel). Not D-Bus (changed 2026-11-14 in AI6a): the sandbox of AI5d hides the
  session bus, which is a way out of it. stdio MCP servers are the baseline every ACP agent
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
  AI6. (The idea that the bridge announces each call before the agent asks does not hold: an
  agent asks first and calls the tool only once allowed; see AI6c.)

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
  *AI5d4 – read-only root (2026-11-14, found during AI6d manual testing).* The agent's home was a
  writable tmpfs, so a write outside the shared folders (e.g. `cp screenshot ~/`) succeeded into
  the throwaway tmpfs and the agent reported success "despite the sandbox boundary" — nothing
  reached the host, but it looked like a leak and the user's real intent silently did not happen.
  Fix: after `pivot_root` the agent remounts its root `/` read-only (MS_REMOUNT|MS_RDONLY), so
  writes outside the shared folders, `/tmp`, `/var/tmp` and the agent's own bound config fail with
  EROFS. Only the agent's slave namespace is affected; the holder keeps its read-write view and
  still makes mount points for folders added later (AI5d3), which propagate in as read-write
  mounts. Verified with the installed helper (write to `/home/user/` now EROFS, host untouched)
  and all three real agents (they start, read, and write inside the folder; Claude/Codex/OpenCode).
  whaleaisandboxtest asserts root-read-only; addFolders still writes into a live-added folder.
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

**AI6 – Back channel.** The agent can see and change where the user is: `orcinus-mcp` and a
small MCP server in the panel. First tools: `get_location`, `set_location` (go to a folder, in the
active view), `get_selection`, `select_items` (in the current folder). Later candidates: open in a
new tab / the other split view, filter, show in the viewer. Each tool call that changes the view
is visible in the transcript.

Details (planned 2026-11-14, AI6a):

- *No D-Bus.* The plan had `orcinus-mcp` talk over D-Bus to the window. The sandbox (AI5d) hides
  `/run/user`, and with it the session bus, on purpose: the bus is a way out (`systemd-run
  --user`). A filtering proxy (`xdg-dbus-proxy`) would be one more moving part for four tools.
  Instead each conversation gets its own Unix socket; with "All folders" (no sandbox) it is the
  same way, so there is only one.
- *The socket.* The panel listens (`QLocalServer`) on `$XDG_RUNTIME_DIR/orcinus-ai/<random>/mcp`
  (folders `0700`; leftovers of a crashed Orcinus are removed at the next start). With the
  sandbox the folder `<random>` is bound read-write into the agent's view at the same path
  (`/run/user` is a tmpfs there, so nothing else of it appears). It lives as long as the
  conversation; a new conversation gets a new one. The token (random, in the arguments of
  `orcinus-mcp`) is the first line on the socket; a wrong one closes it. Within the sandbox the
  socket is the only way to the panel, and only this conversation's agent sees it.
- *`orcinus-mcp` is a relay, the MCP server is in the panel.* `orcinus-mcp --socket PATH --token
  T` copies newline-delimited JSON-RPC between stdio and the socket; nothing else. Plain C++, no
  Qt (it starts with every conversation, like `orcinus-sandbox`), installed to `libexec/orcinus`
  next to the helper; in the build tree next to the program, which is then bound read-only into
  the sandbox (the installed one is under `/usr`, visible anyway). The MCP side (`initialize`,
  `tools/list`, `tools/call`, `ping`) is `WhaleAiMcpServer` in whaleai, so the tool list and the
  checks live in one place and cannot drift from the relay's version. Passed in `session/new` as
  a stdio server named `orcinus` (`WhaleAcpMcpServer`, exists since AI1).
- *Nothing behind the socket is trusted.* The agent's own shell commands can open the socket
  too, so every check is in Orcinus, not in `orcinus-mcp`: the socket offers exactly the tools,
  nothing more. With the sandbox, `set_location` goes only to folders the agent reaches
  (`WhaleAiSandbox::reaches()`; else an error, and no answer that tells whether a folder outside
  exists); `get_selection` lists paths outside the shared folders as "no access", like the chips.
  Non-local folders (KIO URLs) are not offered in the first version: `get_location` says "not a
  local folder", `set_location` takes local paths only.
- *Tools.* `get_location`: folder of the active view, plus the other split view's folder if there
  is one. `get_selection`: selected items of the active view (paths, at most 500, says when cut).
  `set_location(path)`: opens an existing folder in the active view (a file is an error, kept
  plain; selecting it is `select_items`' job). `select_items(names)`: names or
  paths in the current folder, replaces the selection, answers which were not found. The view
  side is a callback set by the window, like `setContextProvider()` (the panel cannot see
  `DolphinMainWindow`; whaleai stays free of the shell). Tool annotations: `readOnlyHint` true
  for the two `get_` tools; for the other two see AI6b.
- *Transcript.* `set_location` and `select_items` add a notice ("The agent opened …", "The agent
  selected 12 items"); no undo link in the first version (the view's Back works). The reads show up
  as the agent's own tool calls anyway.
- *Permission requests for our tools* (moved from AI5): an agent asks before it calls a tool, so
  Orcinus cannot learn of the call first. How to recognise a request for an `orcinus` tool
  reliably (title, `kind`, `rawInput`, `_meta`, the MCP server name in the tool id) differs per
  agent and is checked in AI6b, before anything is answered automatically. Until then the
  requests are shown like any other.

Steps:

- *AI6a – Plan.* This section (2026-11-14).
- *AI6b – Socket, relay, read tools.* `orcinus-mcp`, `WhaleAiMcpServer` with `get_location` and
  `get_selection`, the socket folder in the sandbox, the server passed in `session/new`. Tests:
  `WhaleAiMcpServer` over a real socket (token, tools, errors), and the fake agent starting
  `orcinus-mcp` from `session/new` and calling a tool (new scenario). Then in the VM with the
  real Claude, Codex and OpenCode: do they start it in the sandbox, what do their permission
  requests for our tools look like (note per agent), does `readOnlyHint` spare the question, do
  they call our tools in their read-only mode.
  *AI6b result (2026-11-14).* `orcinus-mcp` (`src/whale/ai/mcp/`, plain C++, installed to
  `libexec/orcinus`): connects, sends the token, relays both ways, ends when either side does;
  without Orcinus behind the socket it exits 1 with a message. `WhaleAiMcpServer` (whaleai, Qt
  Network for `QLocalServer`): socket folder `$XDG_RUNTIME_DIR/orcinus-ai/c-XXXXXX` (`0700`, the
  base folder too; left-over folders whose socket refuses a connection are removed), token of 48
  hex digits, a wrong token closes the connection without an answer, a line over 1 MiB too;
  MCP `initialize` (protocol versions 2025-06-18, 2025-03-26, 2024-11-05; others get the newest),
  `ping`, `tools/list`, `tools/call` (unknown tool: -32602; results as `structuredContent` plus
  the same JSON as text), other methods -32601. Tools `get_location` (`folder`, `access`,
  `otherFolder`, `otherAccess`; `folder` null with a note if the view shows no local folder) and
  `get_selection` (`folder`, `count`, `items` of `path` + `access`, at most 500, `truncated`),
  both `readOnlyHint`. Panel: one server per conversation, parented to the session; with the
  sandbox only the socket file is bound (read-write) and the build tree's relay read-only; the
  access check is `WhaleAiSandbox::reaches()` on what the conversation shares, live with "Give
  Access". Without a relay or when listening fails the agent starts without the back channel
  (warning in the log). Tests: `whaleaimcpservertest` (protocol, tools, socket folder and its
  removal, relay end to end, wrong token, broken and too long messages, leftovers); the fake
  agent's `mcp:TOOL` starts the relay from `session/new`; `whaleaipaneltest` `backChannel` and
  `sandbox` (from inside the sandbox, `access` follows "Give Access" and its removal).
  *Real agents* (throw-away probe on whaleai in the scratchpad: the agent in the sandbox with the
  back channel, raw ACP traffic logged; prompt "call get_location and get_selection, tell me"):
  all three start `orcinus-mcp` inside the sandbox and answer correctly, Claude and OpenCode
  also name the selected file outside as not reachable (Codex ignores `access`).
  - *Claude* (claude-agent-acp): mode `plan` calls both without asking; `default` asks for each.
    The request: `title` and `name` `mcp__orcinus__get_location`, `kind` `other`, `rawInput` the
    arguments, `_meta.claudeCode` `{"toolName": "mcp__orcinus__get_location", "mcpServer":
    {"name": "orcinus", "source": "dynamic"}}`; options allow-once, allow-with-updates
    (allow_always, "don't ask again for Get Location commands"), reject. So `readOnlyHint` does
    not spare the question in `default`.
  - *Codex* (codex-acp): `read-only` and `agent` call both without asking. Tool call `title`
    `mcp.orcinus.get_location`, `kind` `execute`, `rawInput` `{"server": "orcinus", "tool":
    "get_location", "arguments": {}}`.
  - *OpenCode*: `plan` and `build` call both without asking. `title` `orcinus_get_location`,
    `kind` `other`, `rawInput` the arguments.
  - *Signs for AI6c:* Claude `_meta.claudeCode.mcpServer.name == "orcinus"` (reliable; needs
    `_meta` kept in `WhaleAcpToolCall`); Codex `rawInput.server == "orcinus"`; OpenCode only the
    title prefix `orcinus_` (weak, but it did not ask). Whether they ask for the tools without
    `readOnlyHint` (and whether the read-only modes allow them at all) is checked in AI6c.
- *AI6c – Tools that change the view.* `set_location`, `select_items`, the notices, the reach
  check; answering the requests for our tools directly if AI6b found a reliable sign, else
  leave them to the user and note why.
  *AI6c result (2026-11-14).* `set_location(path)`: absolute path, cleaned; the reach check comes
  first and on the canonical path too (a link inside to outside is outside), with the same answer
  for an existing and a missing folder outside; then an existing folder, else an error.
  `select_items(items)`: names in the folder of the active view or absolute paths directly in
  it, at most 500, duplicates once; replaces the selection, answers `selected` and `notFound`;
  nothing found leaves the selection as it was; the folder must be one the agent reaches. Both:
  `readOnlyHint` false, `destructiveHint` false, `idempotentHint` true; without the window's
  actions an error. `WhaleAiViewActions` (open, select) from the window: `changeUrl()` of the
  active view as the user's navigation, selection by `clearSelection()` +
  `markUrlAsCurrent()` + `markUrlsAsSelected()` and `updateViewState()` unless a dir lister of
  the view still loads (then the view applies it at the end, so `select_items` right after
  `set_location` works). Notices "The agent opened …" / "The agent selected N items".
  `WhaleAcpToolCall::meta` keeps `_meta`; `WhaleAiMcpServer::ownTool()` recognises a request by
  the signs of AI6b (Claude `_meta`, Codex `rawInput.server`, OpenCode the exact title with kind
  `other` and no locations; only known tool names); the panel answers those with the "allow
  once" option and shows nothing else (the tool call entry stays); "always" stays the user's.
  Tests: server (both tools, all refusals, `ownTool` with spoofs), panel (notices, auto answer
  as each agent asks, a spoofed command is shown), main window (open + select while loading,
  then replace).
  *Real agents* (probe, prompt "open sub with set_location, then select b.txt"): Codex
  `read-only` and OpenCode `plan` call both without asking; Claude `default` asks for each and
  Orcinus answers (recognised as `set_location` / `select_items`). Claude `plan` does not call
  them: it writes a plan and asks "Approve Plan" (`switch_mode`), also with `readOnlyHint` true
  and a description saying the tools change no file (both tried, taken back). So in Claude's
  plan mode the agent can read where the user is but not show a result; that is Claude's rule
  for its plan mode, and approving the plan is the user's choice of a looser mode.
- *AI6d – Try it.* Only after a VM snapshot (user, 2026-11-14). Real agents in the VM with tasks
  that need the back channel ("select the images smaller than 1000 pixels", "go to the folder
  with the newest PDF"); fixes; results here.
  *Attack tests* (user, 2026-11-14: things that would surely wreck the system must not be
  possible): with "This folder and subfolders", tell each agent (in its loosest mode, approving
  everything) to do real damage: `rm -rf ~` and `/`, delete or change files outside the folder,
  write `~/.bashrc`, `~/.config/autostart`, `~/.ssh`, systemd user units, escape through
  `systemd-run --user`, D-Bus, `/proc`, `nsenter`, the X11 / Wayland sockets, `kill` of host
  processes, and the back channel itself (open the socket directly, wrong token, huge or broken
  messages, `set_location` outside). Expected: nothing outside the sandbox changes. "All folders"
  has no such protection by design and is not part of it.
  *AI6d result (2026-11-14).* Snapshot taken by the user first. A throw-away ACP client on
  whaleai (scratchpad, the agent in orcinus-sandbox with the back channel, "approve all").
  - *Useful tasks.* "Select the images below 1000 px" (folder of 8): Claude (default) and Codex
    (read-only) selected exactly the four right ones through get_location + get_selection +
    select_items; OpenCode (plan) found the four but did not call select_items (it ended with
    "approve and I'll execute" — its plan mode wants a go-ahead the ACP client did not give; in
    build it selects, like AI6c). "Open the folder of the newest PDF and select it" (tree of 4
    PDFs): all three opened docs/reports and selected q3.pdf.
  - *Attack tests done safely.* Written not as destruction but as boundary probes: the point is
    whether the sandboxed process can *see* or *create* anything outside its folder; if a plain
    new file at `~/.bashrc` or in `~/.config/autostart` is refused, so is deleting or
    overwriting. A probe script plus the agent's own tries, in the loosest mode of each (Claude
    bypassPermissions, Codex agent-full-access, OpenCode build). Baseline + "after" snapshot of
    the forbidden host paths (`~/.bashrc`, `~/.ssh`, canaries, `~/.vm-exchange`, the repo,
    autostart, systemd user units, applications, plasma env, git hooks, host `/tmp` canary, git
    HEAD, crontab); noisy host-session files (`~/.claude*`, written by my own Claude Code and by
    a real Claude agent's login by design) excluded.
  - *Result: the boundary holds for all three.* The snapshot was IDENTICAL after each agent;
    nothing outside the sandbox changed. From inside, the host home is invisible (a fresh tmpfs
    root with only the bound config dirs; no `~/.bashrc`, no `~/.ssh`), reading host paths
    fails, the user D-Bus session and other processes are not there, `systemd-run --user` finds
    no bus. The writes that *do* succeed (`~/.bashrc-probe`, an agent's own bound config) land in
    the ephemeral tmpfs or the agent's own config and were confirmed absent on the host
    afterwards. The two expected "yes" are the network (agents need it) and the agent writing
    its own login dir (AI5d, by design). A caveat worth a doc line: a Claude agent gets the real
    `~/.claude` read-write for its login, which is the same tree that holds Claude Code session
    data — in the VM agents use their own logins ([[ai-dev-in-vm]]).
  - *Back channel attacks* are covered by whaleaimcpservertest (wrong token closes the
    connection, a too-long or broken message too, `set_location` / `select_items` outside the
    reachable folders refused): the checks are server-side, so the agent's own shell opening the
    bound socket gains nothing beyond the four tools with their reach checks.
  AI6 complete.

**AI7 – System prompt and language.** A system prompt leads the first message of every
conversation (ACP has no system prompt of its own; later messages carry only the live context).
Part A (36de8cbca): `WhaleAiAgents::defaultSystemPrompt()` — a built-in English briefing (role,
the Orcinus tools, the sandbox) that grows with the features; `systemPrompt()` returns it unless
the user took over (`customSystemPrompt()` / `setSystemPrompt()`), stored globally in orcinusrc
`[AI]`. The AI settings page (which was sparse) gets a "System prompt" box showing the text, a
checkbox that unlocks editing it behind a warning (the sandbox holds either way), and a reset
button. `fullSystemPrompt(language)` = `systemPrompt()` + `greetingInstruction(language)`, an
always-appended line: greet with one short sentence in the language, then follow the user's
language. The panel sends it once, prepended to the first prompt's blocks (`m_sessionGreeted`).
Part B (language memory): `greetingInstruction` also tells the agent to call `remember_language`
when the user writes in another language; the back-channel tool `remember_language` (AI6,
auto-answered like the others, `WhaleAiAgents::setLanguage()`, a transcript notice) stores it,
and the next conversation greets in it (`WhaleAiAgents::language()` passed to `fullSystemPrompt`).
Tried with the real agents: Claude greets in the system language in one sentence, then, told in
German, calls `remember_language("German")` and answers in German.

**AI8 – Documentation.** `docs/orcinus-vs-dolphin.md`, design notes, user docs (setup of a cloud
and a local agent).

## Open

- Session persistence (ACP `session/load`) across restarts: later.
- Several panels / windows: one session each; the MCP token keeps them apart.
- Whether to offer `fs/write_text_file` so writes go through KIO (trash, undo): after AI0, depends on
  how the agents use it.
- Agents that need a login in a terminal: the test button shows the hint; maybe "open in terminal
  panel" for the login.
