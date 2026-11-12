# AI support (AI0–AI7)

Branch `ai-support`, started 2026-11-12. Status: plan, not reviewed yet.

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

**AI1 – ACP client core.** `AcpConnection` (QProcess, newline-delimited JSON-RPC, requests with
ids, notifications, errors, cancel), `AcpSession` (initialize, `session/new`, `session/prompt`,
`session/update` stream, `session/cancel`, `session/set_mode`, permission requests as signals).
Client capabilities: none of `fs/*` or `terminal/*` in the first version (the agent uses its own
tools). Unit tests against a fake agent (small script that replays recorded traffic from AI0).

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
"always" per session only in the first version. Settings dialog with details only after we have
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
