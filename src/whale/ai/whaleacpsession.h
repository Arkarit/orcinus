/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEACPSESSION_H
#define WHALEACPSESSION_H

#include "whaleacptypes.h"

#include <QHash>
#include <QObject>
#include <QProcessEnvironment>

class WhaleAcpConnection;

/**
 * @brief One conversation with an ACP agent: the agent process and one session in it.
 *
 * start() launches the agent, sends `initialize` and `session/new` (with the working directory
 * and the MCP servers, e.g. the back channel into Orcinus). Then prompts can be sent one at a
 * time; the agent's answer streams in as signals until promptFinished().
 *
 * Orcinus is a plain client: it offers neither the `fs` nor the `terminal` methods, the agent works with its
 * own tools. Requests for them are answered with "method not found". Permission requests become
 * permissionRequested(); each must be answered with answerPermission(). cancel() answers the
 * open ones with "cancelled", as ACP demands.
 *
 * Settings of the session (mode, model, …) are configOptions(). Agents that only know the older
 * session modes get one option made from them (WhaleAcpConfigOption::modesOptionId()).
 *
 * Session updates of a kind Orcinus does not know are ignored; all of them are available raw
 * through sessionUpdate().
 */
class WhaleAcpSession : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle, ///< not started
        Starting, ///< agent process, `initialize`, `session/new`
        Ready, ///< waiting for a prompt
        Prompting, ///< a prompt turn runs
        Failed, ///< see errorString(); the agent is stopped
        Closed, ///< close() was called
    };
    Q_ENUM(State)

    struct AgentCommand {
        QString program;
        QStringList arguments;
        QProcessEnvironment environment; ///< empty: the one of Orcinus
    };

    explicit WhaleAcpSession(QObject *parent = nullptr);
    ~WhaleAcpSession() override;

    /** Starts the agent and a session in @p workingDirectory (absolute). */
    void start(const AgentCommand &command, const QString &workingDirectory, const QList<WhaleAcpMcpServer> &mcpServers = {});

    /**
     * Sends a prompt (ACP content blocks, see WhaleAcpContent).
     * @return False if no prompt can be sent now (not Ready).
     */
    bool prompt(const QJsonArray &content);

    /** Cancels the running prompt turn; promptFinished("cancelled") follows. */
    void cancel();

    /** Ends the agent. No more signals except stateChanged(Closed). */
    void close();

    /** Answers the permission request @p key with the option @p optionId. */
    void answerPermission(const QString &key, const QString &optionId);

    /**
     * Sets the option @p id to @p value. The agent answers with all options, which may change
     * others too (e.g. a model with other reasoning levels); configOptionsChanged() follows.
     */
    void setConfigOption(const QString &id, const QString &value);

    State state() const;
    QString errorString() const;
    /** @return The error code of the failure, e.g. WhaleAcpError::AuthRequired. */
    int errorCode() const;
    WhaleAcpAgentInfo agentInfo() const;
    QString sessionId() const;
    QList<WhaleAcpConfigOption> configOptions() const;
    /** @return The first option of @p category ("mode", "model", …), or nullptr. */
    const WhaleAcpConfigOption *configOption(const QString &category) const;
    QList<WhaleAcpCommand> availableCommands() const;
    QList<WhaleAcpPermissionRequest> pendingPermissions() const;
    WhaleAcpToolCall toolCall(const QString &id) const;
    /** @return The last lines the agent wrote to stderr, for error messages. */
    QString agentLog() const;

    /** Timeouts of `initialize` and `session/new`, for tests. */
    void setStartTimeout(int milliseconds);

Q_SIGNALS:
    void stateChanged(WhaleAcpSession::State state);
    void configOptionsChanged();
    void availableCommandsChanged();
    void agentMessageChunk(const QString &text);
    void agentThoughtChunk(const QString &text);
    /** A tool call started or changed; @p toolCall holds all that is known about it. */
    void toolCallChanged(const WhaleAcpToolCall &toolCall);
    void permissionRequested(const WhaleAcpPermissionRequest &request);
    /** The request @p key needs no answer any more (turn cancelled or agent ended). */
    void permissionWithdrawn(const QString &key);
    /** Context used, of the window size (`usage_update`); 0 if unknown. */
    void usageChanged(qint64 used, qint64 size);
    /** The prompt turn ended: end_turn, max_tokens, max_turn_requests, refusal, cancelled, or "error". */
    void promptFinished(const QString &stopReason);
    /** A `session/update` of this session, raw, including the kinds handled above. */
    void sessionUpdate(const QString &kind, const QJsonObject &update);
    /** The agent broke the protocol in a way the session survives; for the log. */
    void protocolWarning(const QString &message);

private:
    void setState(State state);
    void fail(const QString &message, int code = WhaleAcpError::InternalError);
    void initialize();
    void createSession(const QString &workingDirectory, const QList<WhaleAcpMcpServer> &mcpServers);
    void handleNotification(const QString &method, const QJsonObject &params);
    void handleRequest(const QJsonValue &id, const QString &method, const QJsonObject &params);
    void handleUpdate(const QJsonObject &update);
    void readConfig(const QJsonObject &result);
    void withdrawPermissions();
    void cancelOpenToolCalls();

    WhaleAcpConnection *m_connection = nullptr;
    State m_state = State::Idle;
    QString m_errorString;
    int m_errorCode = WhaleAcpError::None;
    WhaleAcpAgentInfo m_agentInfo;
    QString m_sessionId;
    QList<WhaleAcpConfigOption> m_configOptions;
    bool m_hasConfigOptions = false; ///< the agent sends configOptions, not only modes
    QList<WhaleAcpCommand> m_commands;
    QHash<QString, WhaleAcpToolCall> m_toolCalls;
    struct PendingPermission {
        QJsonValue id;
        WhaleAcpPermissionRequest request;
    };
    QList<PendingPermission> m_permissions;
    struct PendingStart {
        QString workingDirectory;
        QList<WhaleAcpMcpServer> mcpServers;
    };
    PendingStart m_pendingStart;
    int m_startTimeout = 60000;
    qint64 m_promptRequest = -1;
};

#endif
