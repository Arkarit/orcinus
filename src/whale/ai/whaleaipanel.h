/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIPANEL_H
#define WHALEAIPANEL_H

#include "panels/panel.h"
#include "whaleacpsession.h"
#include "whaleaiagents.h"
#include "whaleaicontext.h"
#include "whaleaiconversationstore.h"
#include "whaleaimcpserver.h"

#include <functional>
#include <optional>

#include <QPointer>

class KBusyIndicatorWidget;
class KMessageWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QTimer;
class QToolButton;
class WhaleAiContextChips;
class WhaleAiLocalLease;
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
 * Without agents, the panel offers to set them up (configureRequested()). Before anything else,
 * it shows the disclaimer until the user accepts it (WhaleAiDisclaimer, AI5f).
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
    /** Puts back what the agent changed lastingly in this conversation (AI9e). */
    void undoAgentChanges();

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
    /** @return The id of the conversation being saved (AI14); empty before the first message. */
    QString conversationId() const;
    /** The agent the agent proposed (AI11), waiting for the user's Add… or Discard; nothing otherwise. */
    std::optional<WhaleAiAgent> proposedAgent() const;
    /** Adds the proposed agent as @p agent (what the user confirmed in the dialog). For the tests and the bar. */
    void addProposedAgent(const WhaleAiAgent &agent);
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
    /** What the window lets the back channel do; for the tests. */
    const WhaleAiViewActions &viewActions() const
    {
        return m_viewActions;
    }

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
    /** The user started a new conversation: what belonged to the old one goes (the canvas, CV1). */
    void conversationCleared();

protected:
    bool urlChanged() override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    /** @return @p actions, giving the focus back to the input if it had it (the user is typing). */
    WhaleAiViewActions keepingInputFocus(const WhaleAiViewActions &actions);
    /** A line in the transcript about what the agent did in the window (hidden with the Actions filter). */
    void addActionNotice(const QString &text, const QString &toolTip = QString());
    /** See WhaleAiViewActions::activePane. */
    int activePane() const;
    /** Where the user is: from the provider, else the panel's folder. */
    WhaleAiContext currentContext() const;
    /** Answers a request for one of our own tools (AI6c). */
    bool answerOwnToolRequest(const WhaleAcpPermissionRequest &request);
    void fillAgents(const QString &preferredId);
    /** list_agents and propose_agent (AI11): the agent may read the agents and propose one; list_local_models (AI12). */
    QList<WhaleAiWindowTool> agentTools();
    void showProposal(const WhaleAiAgent &agent);
    /** Add… of the proposal bar: the agent dialog, filled in, for the user to edit and confirm. */
    void reviewProposal();
    /** The disclaimer until it is accepted (AI5f), then the setup without agents, else the chat. */
    void updatePage();
    void agentChosen();
    bool startSession();
    /**
     * Ends the conversation. Its local model goes after the grace period (a new conversation or
     * a restart of the same agent likely follows), or at once with @p freeLocalModelNow (AI12f:
     * another agent, the panel or the window closed).
     */
    void endSession(bool freeLocalModelNow = false);
    /** The panel was closed: the agent of a local model is stopped to free it (AI12f). */
    void freeHiddenLocalModel();
    /** How long a closed panel waits before it frees its local model (setting per provider). */
    int hiddenDelayMs() const;
    /** Shows Orcinus' own greeting at the top of an empty conversation. */
    void greet();
    void sendPending();
    void applyStartMode();
    void requestMode(const QString &value, bool byUser);
    void modeSet(const QString &id, const QString &value, const QString &error);
    void blockForMode(const QString &message);
    void noteModeChange();
    QString modeName(const QString &value) const;
    void updateState();
    /** Shows the busy spinner while the model loads or the agent starts or works (AI13f). */
    void updateBusy();
    /** The start/unload button of an agent with a local model (AI13f). */
    void updateModelButton();
    void toggleLocalModel();
    void updateSessionInfo();
    void openLink(const QString &link);
    void updateModes();
    /** AI14: a new saved conversation from the next entry on, of the session that starts. */
    void beginConversation(const WhaleAiAgent &agent, const QString &folder);
    /** Saves the conversation now (if it has a message of the user and saving is on). */
    void saveConversation();

    QStackedWidget *m_stack = nullptr;
    QComboBox *m_agents = nullptr;
    KMessageWidget *m_proposalBar = nullptr;
    std::optional<WhaleAiAgent> m_proposal;
    QToolButton *m_newConversation = nullptr;
    QToolButton *m_undoChanges = nullptr; ///< "Undo the agent's changes" (AI9e)
    QLabel *m_sessionInfo = nullptr;
    QComboBox *m_modes = nullptr;
    WhaleAiTranscript *m_transcript = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QToolButton *m_send = nullptr;
    KBusyIndicatorWidget *m_busy = nullptr;
    QToolButton *m_modelButton = nullptr; ///< start or unload the local model (AI13f)

    QPointer<WhaleAcpSession> m_session;
    QString m_sessionAgentId;
    bool m_systemPromptSent = false; ///< the system prompt is sent once, with the first message (AI)
    QPointer<WhaleAiMcpServer> m_mcp; ///< the back channel of the conversation (AI6)
    QPointer<WhaleAiLocalLease> m_lease; ///< the local model of the conversation (AI12)
    QString m_localProgress; ///< what the local model is doing while it starts
    QTimer *m_hiddenTimer = nullptr; ///< frees the local model when the panel stays hidden
    QCheckBox *m_showActions = nullptr;
    QCheckBox *m_showTimestamps = nullptr;
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

    std::optional<WhaleAiConversation> m_conversation; ///< what is saved of the conversation (AI14)
    int m_conversationStart = 0; ///< its first entry in the transcript
    QTimer *m_saveTimer = nullptr;
};

#endif
