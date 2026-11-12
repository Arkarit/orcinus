/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIPANEL_H
#define WHALEAIPANEL_H

#include "panels/panel.h"
#include "whaleacpsession.h"

#include <QPointer>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class WhaleAiTranscript;

/**
 * @brief The AI panel (AI3): a conversation with an ACP agent, next to the files.
 *
 * Above: the agent (from the "AI Agents" settings), "New Conversation", and what the session
 * runs with (model, mode), so the user sees where prompts go. In the middle the transcript,
 * below the input: Enter sends, Shift+Enter starts a new line; while the agent works, the send
 * button stops it (as does Escape).
 *
 * The agent is started with the first prompt, in the folder of the active view (the home
 * folder for non-local locations). Hiding the panel keeps the conversation; it ends with
 * "New Conversation", when another agent is chosen, or when the panel goes. If the agent
 * fails, the next prompt starts it again in a new conversation.
 *
 * Without agents, the panel offers to set them up (configureRequested()).
 *
 * See .privatedoc/ai-support-plan.md.
 */
class WhaleAiPanel : public Panel
{
    Q_OBJECT

public:
    explicit WhaleAiPanel(QWidget *parent = nullptr);
    ~WhaleAiPanel() override;

    /** Reloads the agents (after the settings changed). */
    void readSettings() override;

    /** Sends @p text as if the user typed it. @return False if it is empty or no agent is set up. */
    bool sendPrompt(const QString &text);
    /** Stops the agent's current turn. */
    void stop();
    /** Ends the conversation and clears the transcript. */
    void newConversation();

    /** @return The id of the chosen agent. */
    QString agentId() const;
    void setAgentId(const QString &id);

    WhaleAcpSession *session() const;
    WhaleAiTranscript *transcript() const;

Q_SIGNALS:
    /** The user wants to set up agents. */
    void configureRequested();
    /** A local file or folder in an answer was clicked. */
    void localUrlActivated(const QUrl &url);

protected:
    bool urlChanged() override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private:
    void fillAgents(const QString &preferredId);
    void agentChosen();
    bool startSession();
    void endSession();
    void sendPending();
    void updateState();
    void updateSessionInfo();
    void openLink(const QString &link);

    QStackedWidget *m_stack = nullptr;
    QComboBox *m_agents = nullptr;
    QToolButton *m_newConversation = nullptr;
    QLabel *m_sessionInfo = nullptr;
    WhaleAiTranscript *m_transcript = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QToolButton *m_send = nullptr;

    QPointer<WhaleAcpSession> m_session;
    QString m_sessionAgentId;
    QString m_pendingPrompt;
};

#endif
