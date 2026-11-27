# AI Support

## An assistant in the file manager

"Give these 300 photos sensible names." "Why is my home folder so big?" "Build me a tool for sorting out photos quickly." An AI agent can take care of things like these for you, and Orcinus gives it a place right next to your files.

To do this, Orcinus talks to an **AI agent running on your computer**, for example Claude Code, Codex or OpenCode with a local model. You type in a panel. The agent sees which folder you have open and what you've selected, and it can open folders, select files and operate Orcinus for you.

Orcinus itself calls **no AI service**. Login, model, billing and the agent's tools remain the agent's business. Out of the box nothing is set up, and nothing leaves your computer before you add an agent yourself.

How to set up an agent is described in the guide "Setting up AI agents" (`docs/ai-agents.md`).

## Before you start: a serious word

This is where the fun stops for a moment. An agent works **with your permissions**. It can read, change and delete your files, start programs and, if you allow it, ask for administrator rights. Orcinus has no sandbox of its own.

- If AI agents are new to you, try them out in a **virtual machine** first.
- Make **backups** of everything that matters to you.
- If you don't understand what the agent is about to do: **ask it** before you allow it.
- If you use a cloud agent, the contents the agent reads (files, command output, paths) go to the provider's servers. Be aware of that.

When you open the panel for the first time, a dialog explains exactly this. Only after you've confirmed it does the panel work.

## The "AI Assistant" panel

You open it with **F8** or via *View → Panels → AI Assistant*. At the top you choose the agent, start a new conversation, open a saved one or jump to the settings (*Settings → Configure Orcinus… → AI Support*).

![The "AI Assistant" panel](images/orcinus-ai-panel.png)

### Modes and permissions

A freshly set-up agent starts in its **most cautious mode**, such as *Plan* or *Read only*. So it may look around, but it asks before it changes anything. You loosen this with the *Mode* field below the agent's name. The mode you choose there also applies the next time you start.

If the agent wants to do something its mode doesn't allow, **it asks in the conversation**, and you answer there. If a question is waiting, the panel opens by itself so the agent isn't left twiddling its thumbs forever.

### What the agent sees

With every message, the agent gets the folder of the active view, the selected items and the folder of the other half of the split view. The line below your message shows what was sent along. Files you drag onto the input field, copied files and pasted images go along as small "chips".

### What the agent can do in Orcinus

It can open folders, select files, change the view mode, split the view, change a number of settings and profiles and trigger most actions from the menus. Each of these actions shows up as a line in the conversation. The *Actions* checkbox hides them.

#### Limits

Orcinus gives the agent no function for changing its own AI settings. At most it can suggest a new agent, which you then add yourself. An agent could get around that through the file system, though. So: always stay alert.

#### No undo for the agent

Orcinus can't undo what an agent does. It can start programs outside Orcinus, change or delete files and mess with system settings. That's beyond Orcinus' control. When in doubt, make a backup first, or ask the agent to make one.

### Commands

A message starting with `/` is a command and goes straight to the agent, for example `/usage` with Claude (shows the 5-hour and weekly limits) or `/status` with Codex. If you type just `/`, Orcinus lists all commands from Orcinus and from the agent.

### Usage

The line below the agent's name shows the model and how full the context is. Its tooltip lists the conversation's tokens and, if the agent reports them, the costs. With a subscription, these are the costs the same usage would cause through the API. Good to know, but no reason to panic.

### Suggestions

A new conversation offers you a few suggestions to get started: rename files, check the system, report a problem with Orcinus, and so on. A click puts the text into the input field, where you can add to it. Nothing is sent until you press Enter.

### Administrator rights

If it's allowed (*AI Support → Agents → Administrator rights → Let agents ask for administrator rights*, on by default), the agent may run individual commands as administrator via `pkexec`. The system then asks for your password **every single time**. Read carefully beforehand what is about to be run.

## Conversations and memory

- **Saving conversations** (*AI Support → Memory → Conversations*, on by default, for 90 days): the *Saved Conversations* button lists them. An agent that supports it continues a saved conversation exactly where it left off. In the same menu you can **export** the displayed conversation as PDF, Markdown or text.
- **Language:** the agents answer in Orcinus' language or in the one you set in *Talk with agents in*.
- **Memory:** short notes the agent gets in every conversation, for all folders or for one folder and its subfolders. For example: "Invoices go into one folder per year, PDFs keep their names."
  - *Manual* (default): the agent proposes a note, and you keep or discard it.
  - *Automatic*: the agent remembers things by itself.
  - *Off*.

  You can write a note yourself with `/remember …` (for the displayed folder) or `/remember global …`. The notes apply to all agents and are separate from their own memory.
- **System prompt** (*AI Support → System Prompt*): this is what Orcinus tells every agent at the start. You can replace it with your own text.

## Local models

Agents like OpenCode can also work with a **model on your own graphics card** instead of a cloud service. Then nothing leaves your computer (as long as your setup itself doesn't pass on any data), and there's no bill either. Well, except for the electricity bill. And the installments for the graphics card, which can cost anything from a few hundred euros to five figures. But nothing else, promise. You need a card with enough memory for this (with 16 GB you can already do quite a bit, especially with Strata), and a local model is usually slower and less capable than the big cloud models.

Orcinus finds the models of **LM Studio** and **Strata**: *AI Support → Agents → Add… → Local Model (LM Studio)…* (or *(Strata)*) creates an agent for one of them.

Orcinus starts the model server when a conversation needs it and unloads the model again when the conversation ends or the panel stays hidden for a while. That way it doesn't block the graphics memory permanently, and your after-work game doesn't stutter. With the button next to the agent you load or unload the model by hand.

## The AI Canvas and its tools

*View → Panels → AI Canvas* is where the agent shows its work: images and videos it has generated, files it has found, sounds, texts and before-and-after comparisons. Clicking an item shows the file in the view.

![The AI Canvas with a before-and-after comparison](images/orcinus-ai-canvas.png)

If you ask, the agent also builds small **tools** for the canvas: a window of its own with buttons, lists and previews (Qt Quick), optionally with a Python program behind it. For example: "Go through the photos in this folder and let me keep or discard each one." A tool opens as a tab of the canvas. With *Keep on Canvas* it's still there the next time you start.

The Tools button lists your tools and offers **example tools** (*Add Example Tool*, such as *Photo Triage* for sorting out photos).

Tools run with your permissions, just like the agent. That's why the program of a tool you haven't kept yet only starts after you've confirmed it.

## The AI Workbench

If you want to use the assistant and the canvas together, just take the profile **AI Workbench**: the canvas on the left, the Details view in the middle, the assistant on the right. Everything you need at a glance. Since three areas side by side need a lot of room, we recommend at least Full HD (1920 × 1080) for it.
