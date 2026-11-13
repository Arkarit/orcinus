/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIAGENTS_H
#define WHALEAIAGENTS_H

#include "whaleacpsession.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

/**
 * @brief An AI agent the user set up: a program that speaks ACP on stdio.
 */
struct WhaleAiAgent {
    QString id; ///< stable key in orcinusrc
    QString name; ///< shown in the panel
    QString commandLine; ///< program and arguments, quoted as in a shell
    QStringList environment; ///< NAME=value, added to the environment of Orcinus
    QString startMode; ///< value of the "mode" option set after the start (AI5); empty: the agent's

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

/**
 * @return How to log in to @p agent if it is one of the known agents (recognised by its
 *         command line, also through npx), with the login command found in @p searchPaths
 *         (empty: PATH) or run through npx; an invalid hint otherwise.
 */
WhaleAiLoginHint loginHint(const WhaleAiAgent &agent, const QStringList &searchPaths = {});

/** @return An id based on @p base that none of @p agents has. */
QString uniqueId(const QString &base, const QList<WhaleAiAgent> &agents);
}

#endif
