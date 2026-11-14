/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIPANEL_H
#define WHALEAIPANEL_H

#include "panels/panel.h"
#include "whaleacpsession.h"
#include "whaleaicontext.h"
#include "whaleaimcpserver.h"

#include <functional>

#include <QPointer>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class WhaleAiContextChips;
class WhaleAiSandboxControl;
class WhaleAiTranscript;

/**
 * @brief The AI panel (AI3): a conversation with an ACP agent, next to the files.
 *
 * Above: the agent (from the "AI Agents" settings), "New Conversation", what the session runs
 * with (model, context), so the user sees where prompts go, and the permission bar: the agent's
 * modes (AI5b), offered from the last conversation before one runs. In the middle the transcript,
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
 * Before the first message goes out, the conversation is switched to the agent's start mode:
 * the one the user chose last for it, otherwise its read-only mode (AI5). If that fails, or the
 * agent has no read-only mode Orcinus knows, the message waits until the user picks a mode
 * (chooseMode()). Every change of the mode is noted in the transcript.
 *
 * Each message carries the context of the window (AI4): the folder, the selection and the
 * other split view, shown as removable chips above the input, plus files dropped onto the
 * input. The transcript notes what went along.
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

    /**
     * Switches the conversation to the mode @p value and remembers it as the start mode of the
     * agent. A message that waits for a mode goes out once the agent took it. Without a
     * conversation, only the start mode is stored.
     */
    void chooseMode(const QString &value);
    /** @return True while a message waits until the user picks a mode. */
    bool isWaitingForMode() const;

    /** @return The id of the chosen agent. */
    QString agentId() const;
    void setAgentId(const QString &id);

    WhaleAcpSession *session() const;
    WhaleAiTranscript *transcript() const;
    WhaleAiContextChips *contextChips() const;

    /**
     * Sets where the context of a message comes from (the window). Without one, the context is
     * the panel's folder.
     */
    void setContextProvider(const std::function<WhaleAiContext()> &provider);
    /** Asks the provider again (the window's folder, selection or split changed). */
    void refreshContext();
    /** What the agent may do in the window through the back channel (AI6c). */
    void setViewActions(const WhaleAiViewActions &actions);

Q_SIGNALS:
    /** The user wants to set up agents. */
    void configureRequested();
    /**
     * The agent asks for permission while the panel is hidden: an agent that waits unseen
     * looks hung, so the window shows the panel again.
     */
    void attentionRequested();
    /** A local file or folder in an answer was clicked. */
    void localUrlActivated(const QUrl &url);

protected:
    bool urlChanged() override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    /** Where the user is: from the provider, else the panel's folder. */
    WhaleAiContext currentContext() const;
    /** Answers a request for one of our own tools (AI6c). */
    bool answerOwnToolRequest(const WhaleAcpPermissionRequest &request);
    void fillAgents(const QString &preferredId);
    void agentChosen();
    bool startSession();
    void endSession();
    void sendPending();
    void applyStartMode();
    void requestMode(const QString &value, bool byUser);
    void modeSet(const QString &id, const QString &value, const QString &error);
    void blockForMode(const QString &message);
    void noteModeChange();
    QString modeName(const QString &value) const;
    void updateState();
    void updateSessionInfo();
    void openLink(const QString &link);
    void updateModes();
    void updateAccess();
    void chooseAccess(bool allFolders);
    bool answerOutsideRequest(const WhaleAcpPermissionRequest &request);
    void giveAccess(const QString &path);
    void sandboxChanged(bool added, const QString &path, const QString &error);
    void updateShared();

    QStackedWidget *m_stack = nullptr;
    QComboBox *m_agents = nullptr;
    QToolButton *m_newConversation = nullptr;
    QLabel *m_sessionInfo = nullptr;
    QComboBox *m_modes = nullptr;
    QComboBox *m_access = nullptr;
    QLabel *m_shared = nullptr;
    WhaleAiTranscript *m_transcript = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QToolButton *m_send = nullptr;

    QPointer<WhaleAcpSession> m_session;
    QString m_sessionAgentId;
    bool m_sessionAllFolders = true;
    bool m_sessionGreeted = false; ///< the system prompt is sent once, with the first message (AI)
    QStringList m_sessionReach; ///< what the agent shares in its sandbox (AI5d)
    QStringList m_sessionFolders; ///< the folder it started in (AI5d2)
    QStringList m_sessionAdded; ///< what the user shared with it later (AI5d3)
    QPointer<WhaleAiSandboxControl> m_control;
    QPointer<WhaleAiMcpServer> m_mcp; ///< the back channel of the conversation (AI6)
    WhaleAiViewActions m_viewActions;
    QString m_pendingPrompt;
    WhaleAiContext m_pendingContext;
    WhaleAiContextChips *m_chips = nullptr;
    std::function<WhaleAiContext()> m_contextProvider;

    enum class ModeState {
        Unknown, ///< the start mode is not set yet
        Applying, ///< waiting for the agent to take m_modeRequest
        Set,
        Blocked, ///< a message waits until the user picks a mode
    };
    ModeState m_modeState = ModeState::Unknown;
    QString m_modeRequest;
    bool m_modeRequestByUser = false;
    QString m_shownMode; ///< the mode the transcript last named
};

#endif
