<!--
SPDX-FileCopyrightText: 2026 Orcinus contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Setting up AI agents

Orcinus' AI Assistant panel talks to an AI agent that runs on your computer. Orcinus itself
does not include an agent and does not call any AI service: you set up an agent once, and from
then on Orcinus starts it when you write to it. What the panel can do is described in
[Orcinus compared to Dolphin, section 9](orcinus-vs-dolphin.md#9-ai-support).

This page shows two ways: a **cloud agent** (Claude Code or Codex, with your account at
Anthropic or OpenAI) and a **local model** (on your own graphics card, through LM Studio or
Strata and OpenCode). Any other agent that speaks the
[Agent Client Protocol](https://agentclientprotocol.com) works as well (section 4).

## 1. Before you start

An agent works with your rights. Depending on its mode it can read, change and delete your
files, run programs and, if you allow it, ask for administrator rights. Orcinus has no sandbox
of its own; the agents' modes and their questions are the control you have.

- If you are new to AI agents, try them in a virtual machine first. Orcinus runs in one as well.
- Keep backups of what matters to you.
- When you do not understand what the agent wants to do, ask it before you allow it.

The first time you open the panel, a dialog says the same; the panel works once you have
confirmed it.

## 2. A cloud agent: Claude Code or Codex

**You need**

- An account: a Claude subscription or an Anthropic API key for Claude Code; a ChatGPT plan or
  an OpenAI API key for Codex. What the agent does is billed by them, not by Orcinus.
- Node.js with `npx`. Orcinus starts the agent through its ACP adapter
  (`@agentclientprotocol/claude-agent-acp` or `@agentclientprotocol/codex-acp`); if the adapter
  is not installed, `npx` fetches it on the first start. To install it for good instead:
  `npm install -g @agentclientprotocol/claude-agent-acp` (or `…/codex-acp`).

**Steps**

1. **Log in once in a terminal.**
   - Claude: start `claude` and log in there (install it with
     `npm install -g @anthropic-ai/claude-code`, or run it once with
     `npx -y @anthropic-ai/claude-code`).
   - Codex: run `codex login` (`npm install -g @openai/codex`). With an API key instead, set
     `OPENAI_API_KEY` or `CODEX_API_KEY` in the agent's environment in step 2 (*Edit…*).

   The agent in Orcinus uses this login; Orcinus never sees your password.
2. **Add the agent.** *Settings → Configure Orcinus… → AI Support → Agents → Add → Claude* (or
   *Codex*). The entry shows the command Orcinus starts; *Use by Default* makes it the agent of
   new conversations. *Move Up* and *Move Down* set the order of the agent menu in the panel.
3. **Test it.** *Test* starts the agent once and reports its name and version. If it needs a
   login, the result says how to log in, and *Log In in a Terminal…* opens a terminal with the
   right command.
4. **Write to it.** Open the panel with F8 (*View → Panels → AI Assistant*) and write a message,
   e.g. "What is in this folder?". The first message starts the agent in the folder you have
   open.

The agent starts in its most restrictive mode: *Plan* for Claude, *Read-only* for Codex. It can
look around but asks before it changes anything. The *Mode* box loosens it; the mode you choose
there is also the one the agent starts in next time.

To see your limits, send `/usage` to Claude or `/status` to Codex; the tooltip of the line under
the agent's name shows the tokens of the conversation.

## 3. A local model: LM Studio or Strata

A local model runs on your own graphics card: nothing leaves your computer, and there is no
bill, but it needs a card with enough memory and is slower and less capable than the large
cloud models. The agent that works with the model is OpenCode.

**You need**

- A model server:
  - [LM Studio](https://lmstudio.ai) with its command-line tool `lms` and at least one
    downloaded model, or
  - Strata in `~/Strata` with at least one model configuration.
- [OpenCode](https://opencode.ai): `npm install -g opencode-ai`.

**Steps**

1. *Settings → Configure Orcinus… → AI Support → Agents → Add → Local Model (LM Studio)…* (or
   *Local Model (Strata)…*). The entry says when the server or its models are not found.
2. Choose the model and the context length (Orcinus suggests one that fits the model) and
   confirm. Orcinus creates an OpenCode agent for this model; OpenCode's own configuration is not
   changed.
3. Write to it in the panel. The first message starts the server and loads the model, which can
   take a while (Strata: one to three minutes); the panel shows the progress.

Orcinus unloads the model when the conversation ends or the panel stays hidden, so the graphics
memory is free again for other programs. The button next to the agent loads the model ahead of
the first message or unloads it at once. If LM Studio is open already, Orcinus only starts and
stops its server; a Strata server you started yourself keeps running.

## 4. Other agents

*Add → Other Agent…* takes any program that speaks the Agent Client Protocol over its standard
input and output, e.g. Gemini CLI, Goose or Qwen Code: a name, the command line and, if it needs
them, environment variables such as API keys. How the agent logs in and which modes it offers
is up to the agent.

## 5. When something does not work

- **"… cannot be started"**: the command of the agent was not found. Install the agent or its
  adapter (section 2), or check that `npx` is on the path of the session Orcinus runs in.
- **"The agent needs a login first."**: log in as in section 2, step 1; *Test* tells you how.
- **The local model does not start**: the panel shows why (server not found, model missing, not
  enough memory); the details are behind the message. LM Studio needs its `lms` tool on the path.
- **The agent asks before every step**: that is its mode; loosen it with the *Mode* box.
- **The agent answers in another language**: *AI Support → Agents → Language*.
- **More details**: start Orcinus from a terminal with
  `QT_LOGGING_RULES="io.github.arkarit.orcinus.ai.debug=true" orcinus` to see how the agent is
  started and what it writes to its error output.
