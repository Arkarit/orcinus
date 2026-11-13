/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleacpconnection.h"
#include "whaleaidebug.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimer>

namespace
{
constexpr int StderrLines = 40;
constexpr int StopGraceMs = 2000;
constexpr qsizetype MaxLineLength = 32 * 1024 * 1024;
}

WhaleAcpConnection::WhaleAcpConnection(QObject *parent)
    : QObject(parent)
{
}

WhaleAcpConnection::~WhaleAcpConnection()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

qsizetype WhaleAcpConnection::maxLineLength()
{
    return MaxLineLength;
}

void WhaleAcpConnection::start(const QString &program,
                               const QStringList &arguments,
                               const QString &workingDirectory,
                               const QProcessEnvironment &environment,
                               const std::function<void()> &childSetup)
{
    Q_ASSERT(!m_process);
    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(arguments);
    m_process->setWorkingDirectory(workingDirectory);
    if (!environment.isEmpty()) {
        m_process->setProcessEnvironment(environment);
    }
    if (childSetup) {
        m_process->setChildProcessModifier(childSetup);
    }

    connect(m_process, &QProcess::started, this, &WhaleAcpConnection::started);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &WhaleAcpConnection::readStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &WhaleAcpConnection::readStderr);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(QStringLiteral("failed to start: %1").arg(m_process->errorString()));
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        // Lines the agent wrote just before it ended.
        readStdout();
        readStderr();
        if (status == QProcess::CrashExit && !m_stopping) {
            finish(QStringLiteral("crashed"));
        } else if (exitCode != 0 && !m_stopping) {
            finish(QStringLiteral("exited with code %1").arg(exitCode));
        } else if (!m_stopping) {
            finish(QStringLiteral("exited"));
        } else {
            finish(QString());
        }
    });

    qCDebug(WhaleAiDebug) << "starting agent" << program << arguments << "in" << workingDirectory;
    m_process->start(QIODevice::ReadWrite);
}

void WhaleAcpConnection::stop()
{
    if (!isRunning()) {
        return;
    }
    m_stopping = true;
    m_process->closeWriteChannel();
    QTimer::singleShot(StopGraceMs, m_process, [process = m_process] {
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            QTimer::singleShot(StopGraceMs, process, [process] {
                if (process->state() != QProcess::NotRunning) {
                    process->kill();
                }
            });
        }
    });
}

bool WhaleAcpConnection::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning && !m_finished;
}

qint64 WhaleAcpConnection::sendRequest(const QString &method, const QJsonObject &params, const ResponseHandler &handler, int timeoutMs)
{
    const qint64 id = m_nextId++;
    Pending pending{handler, nullptr};

    if (!isRunning()) {
        // Answer later, as an agent would: the caller may not expect a call from inside.
        QTimer::singleShot(0, this, [handler] {
            handler(QJsonValue(), WhaleAcpError{WhaleAcpError::ConnectionClosed, QStringLiteral("The agent is not running."), {}});
        });
        return id;
    }

    if (timeoutMs > 0) {
        pending.timer = new QTimer(this);
        pending.timer->setSingleShot(true);
        connect(pending.timer, &QTimer::timeout, this, [this, id, method] {
            answerPending(id, QJsonValue(), WhaleAcpError{WhaleAcpError::Timeout, QStringLiteral("No answer to %1 in time.").arg(method), {}});
        });
        pending.timer->start(timeoutMs);
    }
    m_pending.insert(id, pending);

    write(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    });
    return id;
}

void WhaleAcpConnection::sendNotification(const QString &method, const QJsonObject &params)
{
    write(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    });
}

void WhaleAcpConnection::sendResult(const QJsonValue &id, const QJsonValue &result)
{
    write(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result},
    });
}

void WhaleAcpConnection::sendError(const QJsonValue &id, int code, const QString &message)
{
    write(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}},
    });
}

QString WhaleAcpConnection::stderrTail() const
{
    QStringList lines = m_stderrLines;
    if (!m_stderrBuffer.isEmpty()) {
        lines.append(QString::fromUtf8(m_stderrBuffer));
    }
    return lines.join(QLatin1Char('\n'));
}

void WhaleAcpConnection::write(const QJsonObject &message)
{
    if (!isRunning()) {
        return;
    }
    QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_process->write(line);
}

void WhaleAcpConnection::readStdout()
{
    if (!m_process) {
        return;
    }
    m_stdoutBuffer.append(m_process->readAllStandardOutput());
    qsizetype start = 0;
    while (!m_finished) {
        const qsizetype end = m_stdoutBuffer.indexOf('\n', start);
        if (end < 0) {
            break;
        }
        const QByteArray line = m_stdoutBuffer.mid(start, end - start).trimmed();
        start = end + 1;
        if (!line.isEmpty()) {
            handleLine(line);
        }
    }
    m_stdoutBuffer.remove(0, start);

    if (m_stdoutBuffer.size() > MaxLineLength && !m_finished) {
        m_stdoutBuffer.clear();
        Q_EMIT protocolError(QStringLiteral("The agent sent a line longer than %1 bytes.").arg(MaxLineLength));
        m_stopping = true;
        m_process->kill();
        finish(QStringLiteral("sent a line that is too long"));
    }
}

void WhaleAcpConnection::readStderr()
{
    if (!m_process) {
        return;
    }
    m_stderrBuffer.append(m_process->readAllStandardError());
    qsizetype newline;
    while ((newline = m_stderrBuffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_stderrBuffer.left(newline));
        m_stderrBuffer.remove(0, newline + 1);
        qCDebug(WhaleAiDebug) << "agent stderr:" << line;
        m_stderrLines.append(line);
        if (m_stderrLines.size() > StderrLines) {
            m_stderrLines.removeFirst();
        }
    }
    // A log without line breaks must not grow without end.
    if (m_stderrBuffer.size() > 64 * 1024) {
        m_stderrBuffer = m_stderrBuffer.right(4096);
    }
}

void WhaleAcpConnection::handleLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(WhaleAiDebug) << "agent sent no JSON object:" << line.left(200);
        Q_EMIT protocolError(QStringLiteral("The agent sent a line that is no JSON-RPC message: %1").arg(QString::fromUtf8(line.left(200))));
        return;
    }

    const QJsonObject message = document.object();
    const bool hasId = message.contains(QLatin1String("id")) && !message.value(QLatin1String("id")).isNull();
    const QString method = message.value(QLatin1String("method")).toString();

    if (!method.isEmpty()) {
        const QJsonObject params = message.value(QLatin1String("params")).toObject();
        if (hasId) {
            Q_EMIT requestReceived(message.value(QLatin1String("id")), method, params);
        } else {
            Q_EMIT notificationReceived(method, params);
        }
        return;
    }

    if (hasId && (message.contains(QLatin1String("result")) || message.contains(QLatin1String("error")))) {
        handleResponse(message);
        return;
    }

    Q_EMIT protocolError(QStringLiteral("The agent sent a message that is neither request, notification nor response."));
}

void WhaleAcpConnection::handleResponse(const QJsonObject &message)
{
    const QJsonValue idValue = message.value(QLatin1String("id"));
    const qint64 id = idValue.isString() ? idValue.toString().toLongLong() : idValue.toInteger(-1);
    if (!m_pending.contains(id)) {
        // Late answer to a request that timed out, or an id we never used.
        qCDebug(WhaleAiDebug) << "response to an unknown request" << idValue;
        return;
    }

    WhaleAcpError error;
    if (message.contains(QLatin1String("error"))) {
        const QJsonObject object = message.value(QLatin1String("error")).toObject();
        error.code = object.value(QLatin1String("code")).toInt(WhaleAcpError::InternalError);
        if (error.code == WhaleAcpError::None) {
            error.code = WhaleAcpError::InternalError;
        }
        error.message = object.value(QLatin1String("message")).toString();
        error.data = object.value(QLatin1String("data"));
    }
    answerPending(id, message.value(QLatin1String("result")), error);
}

void WhaleAcpConnection::answerPending(qint64 id, const QJsonValue &result, const WhaleAcpError &error)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        return;
    }
    const Pending pending = it.value();
    m_pending.erase(it);
    if (pending.timer) {
        pending.timer->deleteLater();
    }
    pending.handler(result, error);
}

void WhaleAcpConnection::finish(const QString &message)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    if (!message.isEmpty()) {
        qCWarning(WhaleAiDebug) << "agent" << message;
    }

    const QList<qint64> ids = m_pending.keys();
    for (const qint64 id : ids) {
        answerPending(id,
                      QJsonValue(),
                      WhaleAcpError{WhaleAcpError::ConnectionClosed, message.isEmpty() ? QStringLiteral("The agent was stopped.") : message, {}});
    }
    Q_EMIT finished(message);
}

#include "moc_whaleacpconnection.cpp"
