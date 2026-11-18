/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIAGENTS_H
#define WHALEAIAGENTS_H

#include "whaleacpsession.h"
#include "whaleailocalmodels.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

/**
 * @brief An AI agent the user set up: a program that speaks ACP on stdio.
 */
struct WhaleAiAgent {
    QString id; ///< stable key in orcinusrc
    QString name; ///< shown in the panel
    QString commandLine; ///< program and arguments, quoted as in a shell
    QStringList environment; ///< NAME=value, added to the environment of Orcinus
    QString startMode; ///< value of the "mode" option set after the start (AI5); empty: the agent's
    /** A local model Orcinus loads before the agent starts and frees again (AI12); empty: none. */
    QString localProvider; ///< "lmstudio"
    QString localModel; ///< the model's id at the provider, e.g. "qwen3.8-27b"
    int localContextLength = 0; ///< 0: the provider's default

    bool usesLocalModel() const
    {
        return !localProvider.isEmpty() && !localModel.isEmpty();
    }

    /**
     * @return The program (found in PATH unless absolute), its arguments and the environment;
     *         an empty program if the command line cannot be used, with the reason in @p error.
     */
    WhaleAcpSession::AgentCommand command(QString *error = nullptr) const;

    bool operator==(const WhaleAiAgent &other) const = default;
};

/**
 * @brief A known agent the settings page offers to add.
 */
struct WhaleAiAgentPreset {
    QString id;
    QString name;
    QString commandLine; ///< what to run; empty if the agent was not found
    QString description;
    QString installHint; ///< how to get it, if it was not found
    bool viaNpx = false; ///< the command line downloads the adapter through npx

    bool isAvailable() const
    {
        return !commandLine.isEmpty();
    }
};

/** The known agents, recognised by their command line (AI5: login). */
enum class WhaleAiAgentKind {
    Unknown,
    Claude,
    Codex,
    OpenCode,
};

/**
 * @brief How to log in to a known agent. The agents' own `authMethods` are written for clients
 *        that run the login themselves (e.g. Codex: "Use ChatGPT"), which Orcinus does not.
 */
struct WhaleAiLoginHint {
    QString text; ///< translated, plain text
    QString command; ///< what to run in a terminal to log in; empty if there is nothing to run
    QUrl url; ///< where to read about the agent and get it

    bool isValid() const
    {
        return !text.isEmpty();
    }
};

/**
 * @brief The agents in orcinusrc [AI], and the known ones to suggest.
 *
 * See .privatedoc/ai-support-plan.md (AI2).
 */
namespace WhaleAiAgents
{
QList<WhaleAiAgent> agents();
/** Stores @p agents in this order; groups of agents that are gone are removed. */
void setAgents(const QList<WhaleAiAgent> &agents);

/** @return The id of the agent the panel starts with; the first agent if none is set. */
QString defaultAgentId();
void setDefaultAgentId(const QString &id);

/** @return The agent with @p id, or one with an empty id. */
WhaleAiAgent agent(const QString &id);

/** Stores @p mode as the start mode of the agent @p id (empty: none); nothing if it is gone. */
void setStartMode(const QString &id, const QString &mode);

/**
 * @return The value of the first choice of @p modes (an option of category "mode") that is
 *         known to only read (Claude and OpenCode `plan`, Codex `read-only`, …); empty if
 *         none is. An agent without a stored start mode starts in it (AI5).
 */
QString restrictiveMode(const WhaleAcpConfigOption &modes);

/**
 * @return The modes the agent @p id offered in its last conversation, as an option of category
 *         "mode" without a current value; no choices if none are known. The permission bar of
 *         the panel offers them before a conversation runs.
 */
WhaleAcpConfigOption knownModes(const QString &id);
/** Remembers the choices of @p modes for the agent @p id; nothing if it is gone. */
void setKnownModes(const QString &id, const WhaleAcpConfigOption &modes);

/**
 * @return The known agents (Claude, Codex, OpenCode), each with the command found in
 *         @p searchPaths (empty: PATH).
 */
QList<WhaleAiAgentPreset> presets(const QStringList &searchPaths = {});

/** @return Which known agent @p agent runs (also through npx), or Unknown. */
WhaleAiAgentKind kind(const WhaleAiAgent &agent);

/**
 * @return How to log in to @p agent if it is one of the known agents (recognised by its
 *         command line, also through npx), with the login command found in @p searchPaths
 *         (empty: PATH) or run through npx; an invalid hint otherwise.
 */
WhaleAiLoginHint loginHint(const WhaleAiAgent &agent, const QStringList &searchPaths = {});

/** @return An id based on @p base that none of @p agents has. */
QString uniqueId(const QString &base, const QList<WhaleAiAgent> &agents);

// ---- local models (AI12) ----

/**
 * @return The context length an agent loads @p model with: 65536 tokens, or the model's maximum
 *         if that is less. An agent needs a long context for its tool descriptions; LM Studio's
 *         own default (about 70000) took 29 GB for a 27B model at Q6 (AI12a).
 */
int defaultContextLength(const WhaleAiLocalModel &model);
/** @return The name a new agent for @p model gets, e.g. "Qwen3.8 27B (local)". */
QString localAgentName(const WhaleAiLocalModel &model);
/**
 * @return OpenCode's configuration (OPENCODE_CONFIG_CONTENT) for @p model of @p provider, loaded
 *         with @p contextLength (0: LM Studio's default, then the model's maximum is assumed).
 */
QString openCodeConfig(const WhaleAiLocalProvider &provider, const WhaleAiLocalModel &model, int contextLength);
/**
 * @return @p environment with OpenCode's configuration for @p model; an OPENCODE_CONFIG_CONTENT
 *         that is not one for LM Studio (the user's own) is kept.
 */
QStringList withOpenCodeConfig(const QStringList &environment, const WhaleAiLocalProvider &provider, const WhaleAiLocalModel &model, int contextLength);
/**
 * @return A new agent for @p model of @p provider, run by @p client (OpenCode), with an id none
 *         of @p agents has; LM Studio and the model start with it.
 */
WhaleAiAgent
localAgent(const WhaleAiLocalProvider &provider, const WhaleAiLocalModel &model, const WhaleAiLocalClient &client, const QList<WhaleAiAgent> &agents);

// ---- for the agent (AI11): it may read the agents and propose one, never change them ----

/** @return @p commandLine with the values of arguments that look like secrets (--api-key x, --token=x, ...) as "…". */
QString maskSecrets(const QString &commandLine);
/** @return @p agent for the agent: id, name, masked command line, start mode, and only the names of its environment variables. */
QJsonObject describeForAgent(const WhaleAiAgent &agent);
/**
 * @return The agent an agent proposes with @p arguments ({"name", "command", "environment"}),
 *         with an id none of @p agents has, or nothing with @p error set (English).
 *         An environment entry "NAME" becomes "NAME=" for the user to fill in.
 */
std::optional<WhaleAiAgent> proposal(const QJsonObject &arguments, const QList<WhaleAiAgent> &agents, QString *error);

/**
 * @return Orcinus' built-in instructions for every agent (role, the Orcinus tools):
 *         the text a fresh setup sends and the settings page shows. It is in English, like the
 *         context of each message, because it goes to the agent; it grows with the features.
 */
QString defaultSystemPrompt();
/** @return The instructions sent to every agent: the user's own if customSystemPrompt(), else defaultSystemPrompt(). */
QString systemPrompt();
/** @return Whether the user edits the system prompt instead of using the built-in one. */
bool customSystemPrompt();
/** Stores the user's own system prompt text (used only while customSystemPrompt()). */
void setSystemPrompt(const QString &text);
/** Lets the user edit the system prompt (@p custom), or go back to the built-in one. */
void setCustomSystemPrompt(bool custom);

/** @return Whether the user accepted the disclaimer about AI agents (AI5f, WhaleAiDisclaimer). */
bool disclaimerAccepted();
void setDisclaimerAccepted(bool accepted);

/** @return Whether the transcript shows what the agent did (the panel's "Actions" filter). */
bool showActions();
void setShowActions(bool show);
}

#endif
