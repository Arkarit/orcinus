/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleacpsession.h"
#include "whaleacpconnection.h"
#include "whaleaidebug.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>

namespace
{
QString errorText(const WhaleAcpError &error)
{
    return error.message.isEmpty() ? QStringLiteral("error %1").arg(error.code) : error.message;
}

QString keyOf(const QJsonValue &id)
{
    if (id.isString()) {
        return QLatin1Char('s') + id.toString();
    }
    return QLatin1Char('n') + QString::number(id.toInteger());
}
}

WhaleAcpSession::WhaleAcpSession(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<WhaleAcpToolCall>();
    qRegisterMetaType<WhaleAcpPermissionRequest>();
}

WhaleAcpSession::~WhaleAcpSession() = default;

void WhaleAcpSession::setStartTimeout(int milliseconds)
{
    m_startTimeout = milliseconds;
}

void WhaleAcpSession::start(const AgentCommand &command, const QString &workingDirectory, const QList<WhaleAcpMcpServer> &mcpServers)
{
    if (m_state != State::Idle) {
        qCWarning(WhaleAiDebug) << "session started twice";
        return;
    }
    if (!QDir::isAbsolutePath(workingDirectory)) {
        fail(QStringLiteral("The working directory must be an absolute path."));
        return;
    }
    setState(State::Starting);

    m_connection = new WhaleAcpConnection(this);
    connect(m_connection, &WhaleAcpConnection::started, this, &WhaleAcpSession::initialize);
    connect(m_connection, &WhaleAcpConnection::notificationReceived, this, &WhaleAcpSession::handleNotification);
    connect(m_connection, &WhaleAcpConnection::requestReceived, this, &WhaleAcpSession::handleRequest);
    connect(m_connection, &WhaleAcpConnection::protocolError, this, &WhaleAcpSession::protocolWarning);
    connect(m_connection, &WhaleAcpConnection::finished, this, [this](const QString &message) {
        withdrawPermissions();
        if (m_state == State::Closed || m_state == State::Failed) {
            return;
        }
        fail(QStringLiteral("The agent %1.").arg(message.isEmpty() ? QStringLiteral("ended") : message), WhaleAcpError::ConnectionClosed);
    });

    // Needed once `initialize` is answered.
    m_pendingStart = {workingDirectory, mcpServers};

    m_connection->start(command.program, command.arguments, workingDirectory, command.environment, command.childSetup);
}

void WhaleAcpSession::initialize()
{
    const QJsonObject params{
        {QStringLiteral("protocolVersion"), WhaleAcpProtocolVersion},
        {QStringLiteral("clientCapabilities"),
         QJsonObject{
             {QStringLiteral("fs"), QJsonObject{{QStringLiteral("readTextFile"), false}, {QStringLiteral("writeTextFile"), false}}},
             {QStringLiteral("terminal"), false},
         }},
        {QStringLiteral("clientInfo"),
         QJsonObject{
             {QStringLiteral("name"), QCoreApplication::applicationName()},
             {QStringLiteral("version"), QCoreApplication::applicationVersion()},
         }},
    };

    m_connection->sendRequest(
        QStringLiteral("initialize"),
        params,
        [this](const QJsonValue &result, const WhaleAcpError &error) {
            if (m_state != State::Starting) {
                return;
            }
            if (error.isError()) {
                fail(QStringLiteral("The agent did not start a connection: %1").arg(error.message), error.code);
                return;
            }
            m_agentInfo = WhaleAcpAgentInfo::fromInitializeResult(result.toObject());
            if (m_agentInfo.protocolVersion != WhaleAcpProtocolVersion) {
                fail(QStringLiteral("The agent speaks ACP version %1, Orcinus version %2.").arg(m_agentInfo.protocolVersion).arg(WhaleAcpProtocolVersion));
                return;
            }
            createSession(m_pendingStart.workingDirectory, m_pendingStart.mcpServers);
        },
        m_startTimeout);
}

void WhaleAcpSession::createSession(const QString &workingDirectory, const QList<WhaleAcpMcpServer> &mcpServers)
{
    QJsonArray servers;
    for (const WhaleAcpMcpServer &server : mcpServers) {
        servers.append(server.toJson());
    }

    m_connection->sendRequest(
        QStringLiteral("session/new"),
        QJsonObject{{QStringLiteral("cwd"), workingDirectory}, {QStringLiteral("mcpServers"), servers}},
        [this](const QJsonValue &result, const WhaleAcpError &error) {
            if (m_state != State::Starting) {
                return;
            }
            if (error.isError()) {
                fail(error.code == WhaleAcpError::AuthRequired ? QStringLiteral("The agent needs a login first: %1").arg(error.message)
                                                               : QStringLiteral("The agent did not start a session: %1").arg(error.message),
                     error.code);
                return;
            }
            const QJsonObject object = result.toObject();
            m_sessionId = object.value(QLatin1String("sessionId")).toString();
            if (m_sessionId.isEmpty()) {
                fail(QStringLiteral("The agent started a session without an id."));
                return;
            }
            readConfig(object);
            setState(State::Ready);
        },
        m_startTimeout);
}

void WhaleAcpSession::readConfig(const QJsonObject &result)
{
    if (result.contains(QLatin1String("configOptions"))) {
        // ACP: configOptions supersede modes; clients that know them ignore modes.
        m_hasConfigOptions = true;
        m_configOptions = WhaleAcpConfigOption::listFromJson(result.value(QLatin1String("configOptions")).toArray());
    } else if (!m_hasConfigOptions && result.value(QLatin1String("modes")).isObject()) {
        m_configOptions = {WhaleAcpConfigOption::fromModes(result.value(QLatin1String("modes")).toObject())};
    } else {
        return;
    }
    Q_EMIT configOptionsChanged();
}

bool WhaleAcpSession::prompt(const QJsonArray &content)
{
    if (m_state != State::Ready) {
        return false;
    }
    setState(State::Prompting);
    m_promptRequest = m_connection->sendRequest(QStringLiteral("session/prompt"),
                                                QJsonObject{{QStringLiteral("sessionId"), m_sessionId}, {QStringLiteral("prompt"), content}},
                                                [this](const QJsonValue &result, const WhaleAcpError &error) {
                                                    m_promptRequest = -1;
                                                    if (m_state != State::Prompting) {
                                                        return;
                                                    }
                                                    // Nothing can need an answer once the turn is over.
                                                    withdrawPermissions();
                                                    if (error.isError()) {
                                                        qCWarning(WhaleAiDebug) << "prompt failed:" << error.code << error.message;
                                                        m_errorString = error.message;
                                                        m_errorCode = error.code;
                                                        if (error.code == WhaleAcpError::ConnectionClosed) {
                                                            // finished() of the connection follows and fails the session.
                                                            Q_EMIT promptFinished(QStringLiteral("error"));
                                                            return;
                                                        }
                                                        setState(State::Ready);
                                                        Q_EMIT promptFinished(QStringLiteral("error"));
                                                        return;
                                                    }
                                                    QString stopReason = result.toObject().value(QLatin1String("stopReason")).toString();
                                                    if (stopReason.isEmpty()) {
                                                        stopReason = QStringLiteral("end_turn");
                                                    }
                                                    if (stopReason == QLatin1String("cancelled")) {
                                                        cancelOpenToolCalls();
                                                    }
                                                    setState(State::Ready);
                                                    Q_EMIT promptFinished(stopReason);
                                                });
    return true;
}

void WhaleAcpSession::cancel()
{
    if (m_state != State::Prompting) {
        return;
    }
    m_connection->sendNotification(QStringLiteral("session/cancel"), QJsonObject{{QStringLiteral("sessionId"), m_sessionId}});
    // ACP: the client answers all open permission requests with "cancelled" and marks the
    // unfinished tool calls as cancelled.
    withdrawPermissions();
    cancelOpenToolCalls();
}

void WhaleAcpSession::close()
{
    if (m_state == State::Closed) {
        return;
    }
    if (m_state == State::Prompting) {
        cancel();
    }
    withdrawPermissions();
    setState(State::Closed);
    if (m_connection) {
        m_connection->stop();
    }
}

void WhaleAcpSession::answerPermission(const QString &key, const QString &optionId)
{
    for (int i = 0; i < m_permissions.size(); ++i) {
        if (m_permissions[i].request.key != key) {
            continue;
        }
        const PendingPermission pending = m_permissions.takeAt(i);
        const bool known = std::any_of(pending.request.options.cbegin(), pending.request.options.cend(), [&optionId](const auto &option) {
            return option.id == optionId;
        });
        if (!known) {
            qCWarning(WhaleAiDebug) << "unknown permission option" << optionId;
        }
        m_connection->sendResult(
            pending.id,
            QJsonObject{{QStringLiteral("outcome"),
                         known ? QJsonObject{{QStringLiteral("outcome"), QStringLiteral("selected")}, {QStringLiteral("optionId"), optionId}}
                               : QJsonObject{{QStringLiteral("outcome"), QStringLiteral("cancelled")}}}});
        return;
    }
}

void WhaleAcpSession::setConfigOption(const QString &id, const QString &value)
{
    if (m_state != State::Ready && m_state != State::Prompting) {
        return;
    }

    if (id == WhaleAcpConfigOption::modesOptionId()) {
        m_connection->sendRequest(QStringLiteral("session/set_mode"),
                                  QJsonObject{{QStringLiteral("sessionId"), m_sessionId}, {QStringLiteral("modeId"), value}},
                                  [this, id, value](const QJsonValue &, const WhaleAcpError &error) {
                                      if (error.isError()) {
                                          Q_EMIT protocolWarning(QStringLiteral("The agent did not change the mode: %1").arg(error.message));
                                          Q_EMIT configOptionSet(id, value, errorText(error));
                                          return;
                                      }
                                      for (WhaleAcpConfigOption &option : m_configOptions) {
                                          if (option.isFromModes()) {
                                              option.currentValue = value;
                                          }
                                      }
                                      Q_EMIT configOptionsChanged();
                                      Q_EMIT configOptionSet(id, value, QString());
                                  });
        return;
    }

    m_connection->sendRequest(QStringLiteral("session/set_config_option"),
                              QJsonObject{{QStringLiteral("sessionId"), m_sessionId}, {QStringLiteral("configId"), id}, {QStringLiteral("value"), value}},
                              [this, id, value](const QJsonValue &result, const WhaleAcpError &error) {
                                  if (error.isError()) {
                                      Q_EMIT protocolWarning(QStringLiteral("The agent did not change the setting: %1").arg(error.message));
                                      Q_EMIT configOptionSet(id, value, errorText(error));
                                      return;
                                  }
                                  readConfig(result.toObject());
                                  Q_EMIT configOptionSet(id, value, QString());
                              });
}

void WhaleAcpSession::handleNotification(const QString &method, const QJsonObject &params)
{
    if (method != QLatin1String("session/update")) {
        qCDebug(WhaleAiDebug) << "ignoring notification" << method;
        return;
    }
    if (params.value(QLatin1String("sessionId")).toString() != m_sessionId || m_sessionId.isEmpty()) {
        qCDebug(WhaleAiDebug) << "update of another session";
        return;
    }
    if (m_state == State::Closed || m_state == State::Failed) {
        return;
    }
    handleUpdate(params.value(QLatin1String("update")).toObject());
}

void WhaleAcpSession::handleUpdate(const QJsonObject &update)
{
    const QString kind = update.value(QLatin1String("sessionUpdate")).toString();

    if (kind == QLatin1String("agent_message_chunk") || kind == QLatin1String("agent_thought_chunk")) {
        const QJsonObject content = update.value(QLatin1String("content")).toObject();
        if (content.value(QLatin1String("type")).toString() == QLatin1String("text")) {
            const QString text = content.value(QLatin1String("text")).toString();
            if (kind == QLatin1String("agent_message_chunk")) {
                Q_EMIT agentMessageChunk(text);
            } else {
                Q_EMIT agentThoughtChunk(text);
            }
        }
    } else if (kind == QLatin1String("tool_call") || kind == QLatin1String("tool_call_update")) {
        const QString id = update.value(QLatin1String("toolCallId")).toString();
        if (!id.isEmpty()) {
            WhaleAcpToolCall &toolCall = m_toolCalls[id];
            toolCall.merge(update);
            Q_EMIT toolCallChanged(toolCall);
        }
    } else if (kind == QLatin1String("config_option_update")) {
        readConfig(update);
    } else if (kind == QLatin1String("current_mode_update")) {
        const QString modeId = update.value(QLatin1String("currentModeId")).toString();
        for (WhaleAcpConfigOption &option : m_configOptions) {
            if (option.isFromModes()) {
                option.currentValue = modeId;
                Q_EMIT configOptionsChanged();
            }
        }
    } else if (kind == QLatin1String("available_commands_update")) {
        m_commands.clear();
        const QJsonArray commands = update.value(QLatin1String("availableCommands")).toArray();
        for (const QJsonValue &value : commands) {
            const QJsonObject command = value.toObject();
            m_commands.append({command.value(QLatin1String("name")).toString(),
                               command.value(QLatin1String("description")).toString(),
                               command.value(QLatin1String("input")).toObject().value(QLatin1String("hint")).toString()});
        }
        Q_EMIT availableCommandsChanged();
    } else if (kind == QLatin1String("usage_update")) {
        Q_EMIT usageChanged(update.value(QLatin1String("used")).toInteger(), update.value(QLatin1String("size")).toInteger());
    }

    Q_EMIT sessionUpdate(kind, update);
}

void WhaleAcpSession::handleRequest(const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    if (method != QLatin1String("session/request_permission")) {
        // No fs/* or terminal/*: not advertised in initialize.
        m_connection->sendError(id, WhaleAcpError::MethodNotFound, QStringLiteral("%1 is not supported by this client.").arg(method));
        return;
    }
    if (params.value(QLatin1String("sessionId")).toString() != m_sessionId || m_state != State::Prompting) {
        m_connection->sendResult(id, QJsonObject{{QStringLiteral("outcome"), QJsonObject{{QStringLiteral("outcome"), QStringLiteral("cancelled")}}}});
        return;
    }

    WhaleAcpPermissionRequest request;
    request.key = keyOf(id);
    const QJsonObject toolCall = params.value(QLatin1String("toolCall")).toObject();
    const QString toolCallId = toolCall.value(QLatin1String("toolCallId")).toString();
    // The request carries an update of the tool call; merge it with what is known.
    request.toolCall = m_toolCalls.value(toolCallId);
    request.toolCall.merge(toolCall);
    if (!toolCallId.isEmpty()) {
        m_toolCalls.insert(toolCallId, request.toolCall);
    }
    const QJsonArray options = params.value(QLatin1String("options")).toArray();
    for (const QJsonValue &value : options) {
        const QJsonObject option = value.toObject();
        request.options.append({option.value(QLatin1String("optionId")).toString(),
                                option.value(QLatin1String("name")).toString(),
                                option.value(QLatin1String("kind")).toString()});
    }

    m_permissions.append({id, request});
    Q_EMIT permissionRequested(request);
}

void WhaleAcpSession::withdrawPermissions()
{
    const QList<PendingPermission> permissions = std::exchange(m_permissions, {});
    for (const PendingPermission &pending : permissions) {
        if (m_connection && m_connection->isRunning()) {
            m_connection->sendResult(pending.id,
                                     QJsonObject{{QStringLiteral("outcome"), QJsonObject{{QStringLiteral("outcome"), QStringLiteral("cancelled")}}}});
        }
        Q_EMIT permissionWithdrawn(pending.request.key);
    }
}

void WhaleAcpSession::cancelOpenToolCalls()
{
    for (WhaleAcpToolCall &toolCall : m_toolCalls) {
        if (!toolCall.isFinished()) {
            toolCall.status = WhaleAcpToolCall::Status::Cancelled;
            Q_EMIT toolCallChanged(toolCall);
        }
    }
}

void WhaleAcpSession::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged(state);
}

void WhaleAcpSession::fail(const QString &message, int code)
{
    qCWarning(WhaleAiDebug) << "session failed:" << message;
    m_errorString = message;
    m_errorCode = code;
    withdrawPermissions();
    setState(State::Failed);
    if (m_connection) {
        m_connection->stop();
    }
}

WhaleAcpSession::State WhaleAcpSession::state() const
{
    return m_state;
}

QString WhaleAcpSession::errorString() const
{
    return m_errorString;
}

int WhaleAcpSession::errorCode() const
{
    return m_errorCode;
}

WhaleAcpAgentInfo WhaleAcpSession::agentInfo() const
{
    return m_agentInfo;
}

QString WhaleAcpSession::sessionId() const
{
    return m_sessionId;
}

QList<WhaleAcpConfigOption> WhaleAcpSession::configOptions() const
{
    return m_configOptions;
}

const WhaleAcpConfigOption *WhaleAcpSession::configOption(const QString &category) const
{
    for (const WhaleAcpConfigOption &option : m_configOptions) {
        if (option.category == category) {
            return &option;
        }
    }
    return nullptr;
}

QList<WhaleAcpCommand> WhaleAcpSession::availableCommands() const
{
    return m_commands;
}

QList<WhaleAcpPermissionRequest> WhaleAcpSession::pendingPermissions() const
{
    QList<WhaleAcpPermissionRequest> list;
    for (const PendingPermission &pending : m_permissions) {
        list.append(pending.request);
    }
    return list;
}

WhaleAcpToolCall WhaleAcpSession::toolCall(const QString &id) const
{
    return m_toolCalls.value(id);
}

QString WhaleAcpSession::agentLog() const
{
    return m_connection ? m_connection->stderrTail() : QString();
}

#include "moc_whaleacpsession.cpp"
