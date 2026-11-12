/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEACPCONNECTION_H
#define WHALEACPCONNECTION_H

#include "whaleacptypes.h"

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <functional>

class QTimer;

/**
 * @brief JSON-RPC 2.0 with an agent process over stdio, as ACP transports it.
 *
 * Starts the agent, writes one JSON message per line to its stdin and reads one per line from
 * its stdout. stderr is the agent's log: the last lines are kept for error messages.
 *
 * Requests get their answer through a handler, optionally with a timeout. When the agent ends,
 * every open request is answered with WhaleAcpError::ConnectionClosed before finished() is
 * emitted. Requests and notifications of the agent come as signals; answer requests with
 * sendResult() or sendError().
 *
 * A line that is no JSON is reported with protocolError() and skipped. A line longer than
 * maxLineLength() ends the connection: the agent is not following the protocol.
 */
class WhaleAcpConnection : public QObject
{
    Q_OBJECT

public:
    using ResponseHandler = std::function<void(const QJsonValue &result, const WhaleAcpError &error)>;

    explicit WhaleAcpConnection(QObject *parent = nullptr);
    /** Kills a still running agent (stop() is the gentle way). */
    ~WhaleAcpConnection() override;

    /**
     * Starts @p program. started() or finished() follows. An empty @p environment means the one
     * of Orcinus.
     */
    void start(const QString &program,
               const QStringList &arguments,
               const QString &workingDirectory,
               const QProcessEnvironment &environment = QProcessEnvironment());

    /**
     * Ends the agent: closes its stdin (agents end on EOF), terminates it after a grace period
     * and kills it after another one. finished() follows.
     */
    void stop();

    bool isRunning() const;

    /**
     * Sends a request. @p handler is called exactly once: with the result, with the agent's
     * error, or with ConnectionClosed / Timeout. @p timeoutMs 0 waits forever.
     * @return The id of the request.
     */
    qint64 sendRequest(const QString &method, const QJsonObject &params, const ResponseHandler &handler, int timeoutMs = 0);
    void sendNotification(const QString &method, const QJsonObject &params);
    /** Answers the agent's request @p id. */
    void sendResult(const QJsonValue &id, const QJsonValue &result);
    void sendError(const QJsonValue &id, int code, const QString &message);

    /** @return The last lines the agent wrote to stderr. */
    QString stderrTail() const;

    static qsizetype maxLineLength();

Q_SIGNALS:
    void started();
    /** The agent sent a request; answer it with sendResult() or sendError(). */
    void requestReceived(const QJsonValue &id, const QString &method, const QJsonObject &params);
    void notificationReceived(const QString &method, const QJsonObject &params);
    /** Something the agent sent broke the protocol; the connection goes on. */
    void protocolError(const QString &message);
    /**
     * The agent ended or could not be started. @p message says why ("exited with code 3",
     * "failed to start", …); it is empty after stop() when the agent ended normally.
     */
    void finished(const QString &message);

private:
    struct Pending {
        ResponseHandler handler;
        QTimer *timer = nullptr;
    };

    void readStdout();
    void readStderr();
    void handleLine(const QByteArray &line);
    void handleResponse(const QJsonObject &message);
    void write(const QJsonObject &message);
    void finish(const QString &message);
    void answerPending(qint64 id, const QJsonValue &result, const WhaleAcpError &error);

    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    QStringList m_stderrLines;
    QByteArray m_stderrBuffer;
    QHash<qint64, Pending> m_pending;
    qint64 m_nextId = 1;
    bool m_stopping = false;
    bool m_finished = false;
};

#endif
