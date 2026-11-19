/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEDEVAI_H
#define WHALEDEVAI_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

class DolphinMainWindow;
class WhaleAcpSession;
class WhaleAiPanel;

/**
 * @brief The AI panel for the development automation (AI10a): its state, a conversation started
 * and a prompt sent as the user does, and the answer to a permission request.
 *
 * Development only, like WhaleDevAutomation. A prompt takes the user's path: the panel's input
 * (WhaleAiPanel::sendPrompt()), ACP, the agent, the back channel. The automation never talks to
 * the agent itself. What the agent did is recorded from the signals of the panel's session (tool
 * calls, permission requests, finished turns) and read from the transcript the user sees.
 */
class WhaleDevAi : public QObject
{
    Q_OBJECT

public:
    explicit WhaleDevAi(DolphinMainWindow *window);

    /**
     * @return {available, panelVisible, agentId, agents, disclaimerAccepted, sessionState (none,
     *         idle, starting, ready, prompting, failed, closed), busy (the send button is a stop
     *         button), waitingForMode, mode, modes, conversation (counts new conversations),
     *         prompts (sent in this conversation), turnsFinished, lastStopReason, toolCallCount,
     *         pendingPermissions: [{key, title, kind, ownTool, options: [{id, name, kind}]}]}.
     *         Cheap: it is read by every wait_for_state.
     */
    QJsonObject state() const;

    /**
     * @return {ok, conversation, entries: [{kind, text}] of the transcript (oldest first, also
     *         while the "Actions" filter hides them), toolCalls: [{id, title, kind, status,
     *         ownTool (the back channel's tool, else empty), rawInput, locations}], permissions:
     *         [{key, title, kind, ownTool, answered (option id, empty while open, "withdrawn")}],
     *         stopReasons, error (of a failed session)}.
     */
    QJsonObject transcript() const;

    /**
     * Ends the conversation as "New Conversation" does, after choosing the agent @p agentId
     * (empty: keep) and its start mode @p mode (empty: keep), and shows the panel. The agent
     * starts with the first prompt.
     */
    QJsonObject newConversation(const QString &agentId, const QString &mode);
    /** AI14: opens the saved conversation @p id (empty: the latest) as its menu entry does. */
    QJsonObject openConversation(const QString &id);

    /** Sends @p text as the user does. Fails while the agent is starting or working. */
    QJsonObject sendPrompt(const QString &text);

    /** Clicks the option @p optionId of the open permission request @p key in the transcript. */
    QJsonObject answerPermission(const QString &key, const QString &optionId);

    /** Stops the agent's turn, as the stop button does. */
    QJsonObject stop();

private:
    WhaleAiPanel *panel() const;
    /** Starts recording the signals of the panel's current session, once per session. */
    void attach() const;
    void resetRecord() const;

    DolphinMainWindow *const m_window;

    // What the current conversation did; mutable because state() attaches lazily.
    mutable QPointer<WhaleAcpSession> m_session;
    mutable int m_conversation = 0;
    mutable int m_prompts = 0;
    mutable int m_turnsFinished = 0;
    mutable QStringList m_stopReasons;
    mutable QJsonArray m_toolCalls; ///< in the order they first came
    mutable QJsonArray m_permissions;
    mutable QString m_error;
};

#endif
