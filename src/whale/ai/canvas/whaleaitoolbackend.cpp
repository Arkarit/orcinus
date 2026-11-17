/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitoolbackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <optional>
#include <utility>

namespace
{
/** The most entries a command may have. */
constexpr int MaxArguments = 64;

/** @return @p line (one line of JSON, any value) parsed, or nothing. */
std::optional<QJsonValue> parseLine(const QByteArray &line)
{
    // QJsonDocument only reads arrays and objects: wrap the value in one.
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray('[' + line + ']'), &error);
    if (error.error != QJsonParseError::NoError || document.array().size() != 1) {
        return std::nullopt;
    }
    return document.array().first();
}
}

WhaleAiToolBackend::WhaleAiToolBackend(const WhaleAiTool &tool, QObject *parent)
    : QObject(parent)
    , m_tool(tool)
    , m_command(parseCommand(tool.backend.isEmpty() ? QJsonValue() : QJsonValue(tool.backend), nullptr).value_or(QStringList()))
{
}

WhaleAiToolBackend::~WhaleAiToolBackend()
{
    if (!m_process) {
        return;
    }
    // Orcinus quits or the tab goes: wait here, a process left behind would keep running.
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->closeWriteChannel();
    if (!process->waitForFinished(StopGrace)) {
        process->terminate();
        if (!process->waitForFinished(TerminateGrace)) {
            process->kill();
            process->waitForFinished(TerminateGrace);
        }
    }
    delete process;
}

std::optional<QStringList> WhaleAiToolBackend::parseCommand(const QJsonValue &backend, QString *error)
{
    auto fail = [error]() -> std::optional<QStringList> {
        if (error) {
            *error = QStringLiteral("\"backend\" needs \"command\": a list of strings, e.g. {\"command\": [\"python3\", \"backend.py\"]}.");
        }
        return std::nullopt;
    };
    if (!backend.isObject()) {
        return fail();
    }
    const QJsonValue command = backend.toObject().value(QLatin1String("command"));
    if (!command.isArray() || command.toArray().isEmpty() || command.toArray().size() > MaxArguments) {
        return fail();
    }
    QStringList result;
    for (const QJsonValue &value : command.toArray()) {
        if (!value.isString()) {
            return fail();
        }
        result.append(value.toString());
    }
    if (result.constFirst().trimmed().isEmpty()) {
        return fail();
    }
    return result;
}

QString WhaleAiToolBackend::scratchFolder(const QString &id)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/orcinus/ai/scratch/tools/") + id;
}

bool WhaleAiToolBackend::hasBackend() const
{
    return !m_command.isEmpty();
}

QStringList WhaleAiToolBackend::command() const
{
    return m_command;
}

WhaleAiToolBackend::State WhaleAiToolBackend::state() const
{
    return m_state;
}

QString WhaleAiToolBackend::stateName() const
{
    switch (m_state) {
    case State::None:
        return QStringLiteral("none");
    case State::Waiting:
        return QStringLiteral("waiting");
    case State::Running:
        return QStringLiteral("running");
    case State::Exited:
        return QStringLiteral("exited");
    }
    return QString();
}

int WhaleAiToolBackend::exitCode() const
{
    return m_exitCode;
}

QStringList WhaleAiToolBackend::stderrTail() const
{
    return m_stderrTail;
}

void WhaleAiToolBackend::setWaiting()
{
    if (!hasBackend() || m_process) {
        return;
    }
    setState(State::Waiting);
}

void WhaleAiToolBackend::start()
{
    if (!hasBackend()) {
        return;
    }
    // Messages the tool sent while waiting for the confirmation go out once it runs.
    QList<QJsonValue> queued = m_state == State::Waiting ? std::exchange(m_queue, {}) : QList<QJsonValue>();
    stop();
    m_queue = std::move(queued);
    m_output.clear();
    m_errors.clear();
    m_droppingOutput = false;
    m_stderrTail.clear();

    // A program with a slash is a file of the tool, anything else comes from PATH.
    QString program = m_command.constFirst();
    const QString folder = QDir::cleanPath(m_tool.folder);
    if (program.contains(QLatin1Char('/'))) {
        const QString path = QDir::cleanPath(QDir(folder).absoluteFilePath(program));
        if (!path.startsWith(folder + QLatin1Char('/')) || !QFileInfo(path).isExecutable()) {
            Q_EMIT logLine(QStringLiteral("The backend cannot start: %1 is not an executable file in the tool's folder.").arg(program));
            m_queue.clear();
            setState(State::Exited, -1);
            return;
        }
        program = path;
    } else {
        program = QStandardPaths::findExecutable(program);
        if (program.isEmpty()) {
            Q_EMIT logLine(QStringLiteral("The backend cannot start: the program %1 is not installed (not in PATH).").arg(m_command.constFirst()));
            m_queue.clear();
            setState(State::Exited, -1);
            return;
        }
    }

    const QString scratch = scratchFolder(m_tool.id);
    QDir().mkpath(scratch);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ORCINUS_TOOL_ID"), m_tool.id);
    environment.insert(QStringLiteral("ORCINUS_TOOL_FOLDER"), folder);
    environment.insert(QStringLiteral("ORCINUS_TOOL_SCRATCH"), scratch);
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));

    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(m_command.mid(1));
    m_process->setWorkingDirectory(folder);
    m_process->setProcessEnvironment(environment);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &WhaleAiToolBackend::readOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &WhaleAiToolBackend::readErrors);
    QProcess *process = m_process;
    connect(m_process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (process != m_process) {
            return;
        }
        readOutput();
        readErrors();
        if (!m_output.isEmpty() && !m_droppingOutput) {
            // A last line without its newline.
            handleLine(std::exchange(m_output, {}));
        }
        m_process = nullptr;
        process->deleteLater();
        const int exitCode = status == QProcess::CrashExit ? -1 : code;
        Q_EMIT logLine(status == QProcess::CrashExit ? QStringLiteral("The backend crashed.") : QStringLiteral("The backend ended (exit code %1).").arg(code));
        m_queue.clear();
        setState(State::Exited, exitCode);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process != m_process || error != QProcess::FailedToStart) {
            return;
        }
        Q_EMIT logLine(QStringLiteral("The backend cannot start: %1").arg(process->errorString()));
        m_process = nullptr;
        process->deleteLater();
        m_queue.clear();
        setState(State::Exited, -1);
    });
    m_process->start();
    if (m_process) {
        setState(State::Running);
        flushQueue();
    }
}

void WhaleAiToolBackend::stop()
{
    m_queue.clear();
    if (m_process) {
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        reap(process);
    }
    if (m_state != State::Exited) {
        setState(State::None);
    }
}

void WhaleAiToolBackend::restart()
{
    start();
}

bool WhaleAiToolBackend::send(const QVariant &value)
{
    const QJsonValue json = QJsonValue::fromVariant(value);
    switch (m_state) {
    case State::Running:
        write(json);
        return true;
    case State::Waiting:
        if (m_queue.size() >= MaxQueue) {
            Q_EMIT logLine(QStringLiteral("A message to the backend was dropped: more than %1 are waiting.").arg(MaxQueue));
            return false;
        }
        m_queue.append(json);
        return true;
    case State::None:
    case State::Exited:
        break;
    }
    return false;
}

void WhaleAiToolBackend::setState(State state, int exitCode)
{
    if (state == m_state && exitCode == m_exitCode) {
        return;
    }
    m_state = state;
    m_exitCode = exitCode;
    Q_EMIT stateChanged();
}

void WhaleAiToolBackend::readOutput()
{
    if (!m_process) {
        return;
    }
    m_output += m_process->readAllStandardOutput();
    qsizetype start = 0;
    while (true) {
        const qsizetype end = m_output.indexOf('\n', start);
        if (end < 0) {
            break;
        }
        if (m_droppingOutput) {
            m_droppingOutput = false;
        } else if (end - start > MaxLine) {
            Q_EMIT logLine(QStringLiteral("A line from the backend was dropped: longer than %1 MiB.").arg(MaxLine / (1024 * 1024)));
        } else {
            handleLine(m_output.mid(start, end - start));
        }
        start = end + 1;
    }
    m_output.remove(0, start);
    if (m_output.size() > MaxLine) {
        if (!m_droppingOutput) {
            Q_EMIT logLine(QStringLiteral("A line from the backend was dropped: longer than %1 MiB.").arg(MaxLine / (1024 * 1024)));
        }
        m_droppingOutput = true;
        m_output.clear();
    }
}

void WhaleAiToolBackend::readErrors()
{
    if (!m_process) {
        return;
    }
    m_errors += m_process->readAllStandardError();
    const qsizetype end = m_errors.lastIndexOf('\n');
    // Without a newline, keep it until it grows too long.
    const qsizetype take = end >= 0 ? end + 1 : (m_errors.size() > 64 * 1024 ? m_errors.size() : 0);
    if (take == 0) {
        return;
    }
    const QList<QByteArray> lines = m_errors.left(take).split('\n');
    m_errors.remove(0, take);
    for (const QByteArray &bytes : lines) {
        const QString line = QString::fromUtf8(bytes).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        m_stderrTail.append(line);
        while (m_stderrTail.size() > StderrLines) {
            m_stderrTail.removeFirst();
        }
        Q_EMIT logLine(QStringLiteral("backend stderr: %1").arg(line));
    }
}

void WhaleAiToolBackend::handleLine(const QByteArray &line)
{
    if (line.trimmed().isEmpty()) {
        return;
    }
    const std::optional<QJsonValue> value = parseLine(line);
    if (!value) {
        Q_EMIT logLine(QStringLiteral("backend (not JSON): %1").arg(QString::fromUtf8(line.left(1000)).trimmed()));
        return;
    }
    Q_EMIT message(value->toVariant());
}

void WhaleAiToolBackend::flushQueue()
{
    const QList<QJsonValue> queue = std::exchange(m_queue, {});
    for (const QJsonValue &value : queue) {
        write(value);
    }
}

void WhaleAiToolBackend::write(const QJsonValue &value)
{
    if (!m_process) {
        return;
    }
    QByteArray line = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    // Strip the array again: the backend gets the value itself.
    line = line.mid(1, line.size() - 2);
    m_process->write(line + '\n');
}

void WhaleAiToolBackend::reap(QProcess *process)
{
    // Owned by the application, so a process still ending when Orcinus quits is killed then
    // (QProcess' destructor kills it).
    process->setParent(QCoreApplication::instance());
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    process->closeWriteChannel();
    QTimer::singleShot(StopGrace, process, [process] {
        process->terminate();
        QTimer::singleShot(TerminateGrace, process, [process] {
            process->kill();
        });
    });
}

#include "moc_whaleaitoolbackend.cpp"
