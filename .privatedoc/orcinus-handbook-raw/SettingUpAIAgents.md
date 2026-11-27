# Setting up AI Agents

## Before the first conversation

The AI Assistant panel talks to an AI agent that runs on your computer. Orcinus itself doesn't include an agent and doesn't call any AI service: you set up an agent once, and from then on Orcinus starts it whenever you write to it. What the panel can do is described in the chapter "AI Support".

There are two ways: a **cloud agent** (Claude Code or Codex, with your account at Anthropic or OpenAI) and a **local model** (on your own graphics card, through LM Studio or Strata and OpenCode). Any other agent that speaks the Agent Client Protocol (ACP) works as well.

And yes, the serious word from the chapter "AI Support" applies here too: an agent works with your permissions. Virtual machine, backups, ask before you allow. You know the drill.

## A cloud agent: Claude Code or Codex

### What you need

- **An account:** a Claude subscription or an Anthropic API key for Claude Code; a ChatGPT plan or an OpenAI API key for Codex. Whatever the agent does is billed by them, not by Orcinus.
- **Node.js with `npx`.** Orcinus starts the agent through its ACP adapter (`@agentclientprotocol/claude-agent-acp` or `@agentclientprotocol/codex-acp`). If the adapter isn't installed, `npx` fetches it on the first start. To install it for good instead: `npm install -g @agentclientprotocol/claude-agent-acp` (or `…/codex-acp`).

### Step by step

1. **Log in once in a terminal.**
   - Claude: start `claude` and log in there (install it with `npm install -g @anthropic-ai/claude-code`, or run it once with `npx -y @anthropic-ai/claude-code`).
   - Codex: run `codex login` (install it with `npm install -g @openai/codex`). If you use an API key instead, set `OPENAI_API_KEY` or `CODEX_API_KEY` in the agent's environment in the next step (*Edit…*).

   The agent in Orcinus uses this login. Orcinus never sees your password.
2. **Add the agent:** *Settings → Configure Orcinus… → AI Support → Agents → Add… → Claude* (or *Codex*). The entry shows the command Orcinus starts. *Use by Default* makes it the agent for new conversations, and *Move Up* and *Move Down* set the order of the agent menu in the panel.
3. **Test it:** *Test* starts the agent once and reports its name and version. If it needs a login, the result tells you how, and *Log In in a Terminal…* opens a terminal with the right command.
4. **Write to it:** open the panel with **F8** (*View → Panels → AI Assistant*) and write a message, for example "What's in this folder?". The first message starts the agent in the folder you have open.

The agent starts in its most cautious mode: *Plan* for Claude, *Read-only* for Codex. You loosen it with the *Mode* box (see the chapter "AI Support").

## A local model: LM Studio or Strata

A local model runs on your own graphics card. Nothing leaves your computer, and apart from electricity there's no bill. The agent that works with the model is OpenCode.

### What you need

- **A model server:**
  - LM Studio (`https://lmstudio.ai`) with its command-line tool `lms` and at least one downloaded model, or
  - Strata in `~/Strata` with at least one model configuration.
- **OpenCode** (`https://opencode.ai`): `npm install -g opencode-ai`.

### Step by step

1. *Settings → Configure Orcinus… → AI Support → Agents → Add… → Local Model (LM Studio)…* (or *Local Model (Strata)…*). The entry tells you if the server or its models can't be found.
2. Choose the model and the context length (Orcinus suggests one that fits the model) and confirm. Orcinus creates an OpenCode agent for this model. OpenCode's own configuration isn't changed.
3. Write to it in the panel. The first message starts the server and loads the model. That can take a while (Strata: one to three minutes), so it's a good moment for a coffee. The panel shows the progress.

If LM Studio is open already, Orcinus only starts and stops its server. A Strata server you started yourself keeps running.

## Other agents

*Add… → Other Agent…* takes any program that speaks the Agent Client Protocol over its standard input and output, for example Gemini CLI, Goose or Qwen Code. You enter a name, the command line and, if it needs them, environment variables such as API keys. How the agent logs in and which modes it offers is up to the agent.

## When something doesn't work

- **"… cannot be started":** the agent's command wasn't found. Install the agent or its adapter, or check that `npx` is on the path of the session Orcinus runs in.
- **"The agent needs a login first.":** log in as described above. *Test* tells you how.
- **The local model doesn't start:** the panel shows why (server not found, model missing, not enough memory). The details are behind the message. LM Studio needs its `lms` tool on the path.
- **The agent asks before every step:** that's its mode. Loosen it with the *Mode* box.
- **The agent answers in another language:** *AI Support → Agents → Language → Talk with agents in*.
- **More details:** start Orcinus from a terminal with `QT_LOGGING_RULES="io.github.arkarit.orcinus.ai.debug=true" orcinus`. Then you see how the agent is started and what it writes to its error output.
