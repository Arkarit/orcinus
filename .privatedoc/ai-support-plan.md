# AI support (AI0–AI7)

Branch `ai-support`, started 2026-11-12. Status: AI0, AI1 done (2026-11-12).

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

**AI3 – Panel.** "AI Assistant" panel: agent selector, transcript (user/agent messages as
Markdown via `QTextDocument`, streamed chunks, tool calls as compact collapsible rows with
status), input field (Enter sends, Shift+Enter new line), stop button, new session. One agent
process per panel, started on the first prompt, ended when the panel closes or the agent changes.

**AI4 – Context.** Chips for folder / selection / other split view above the input, follow the
active view, each removable for the next prompt. Drag and drop of files into the input adds them.
Large selections are capped (count, with a "+N more" note).

**AI5 – Permissions.** Inline permission requests; permission bar with the agent's modes; remember
"always" per session only in the first version. Calls of Orcinus' own harmless MCP tools (show a
folder, select) should not need a click each time: answer those requests directly, recognised
reliably (not by the tool title, which differs per agent; e.g. `orcinus-mcp` announces the call). Settings dialog with details only after we have
tried it.

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
