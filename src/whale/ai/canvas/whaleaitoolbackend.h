/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAITOOLBACKEND_H
#define WHALEAITOOLBACKEND_H

#include "whaleaitoolstore.h"

#include <QByteArray>
#include <QJsonValue>
#include <QObject>
#include <QStringList>
#include <QVariant>

class QProcess;

/**
 * @brief The backend of a canvas tool (CV4): an ordinary process the tool's manifest names.
 *
 * {"backend": {"command": ["python3", "backend.py"]}}
 *
 * It runs in the tool's folder with the user's rights; Orcinus does not limit what it does. The
 * tool's QML and the backend talk newline-delimited JSON over stdin and stdout. A stdout line that
 * is not JSON and every stderr line go to logLine() (the tab's log, tool_log). It is never started
 * again by itself: when it ends, the state is Exited with its exit code until restart().
 *
 * Whether it may start at all (the user's confirmation) is the tab's business (CV4b); this class
 * only runs it. See .privatedoc/ai-canvas-plan.md.
 */
class WhaleAiToolBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ stateName NOTIFY stateChanged)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY stateChanged)

public:
    enum class State {
        /** The tool has no backend, or it was stopped. */
        None,
        /** Not started yet: waiting for the user's confirmation. Messages are queued. */
        Waiting,
        Running,
        /** It ended (or could not start); exitCode() says how. */
        Exited,
    };
    Q_ENUM(State)

    /** Longer lines from the backend are dropped. */
    static constexpr qsizetype MaxLine = 8 * 1024 * 1024;
    /** Messages kept while the backend is not running yet. */
    static constexpr int MaxQueue = 100;
    /** The stderr lines kept for the message after it ended. */
    static constexpr int StderrLines = 20;
    /** Milliseconds between closing stdin and terminating, and between terminating and killing. */
    static constexpr int StopGrace = 2000;
    static constexpr int TerminateGrace = 1000;

    explicit WhaleAiToolBackend(const WhaleAiTool &tool, QObject *parent = nullptr);
    /** Stops a running backend, waiting for it (at most StopGrace + TerminateGrace ms). */
    ~WhaleAiToolBackend() override;

    /**
     * @return The command of a manifest's "backend" value, or nothing with @p error set (English)
     *         when it is not a non-empty list of strings.
     */
    static std::optional<QStringList> parseCommand(const QJsonValue &backend, QString *error);
    /** The folder a tool's backend may use for its own files: ~/.local/share/orcinus/ai/scratch/tools/<id>. */
    static QString scratchFolder(const QString &id);

    bool hasBackend() const;
    QStringList command() const;
    State state() const;
    QString stateName() const;
    /** The exit code after it ended; -1 if it crashed or could not start. */
    int exitCode() const;
    /** The last lines it wrote to stderr (at most StderrLines), also after it ended. */
    QStringList stderrTail() const;

    /** Marks it as waiting for the user's confirmation; send() queues meanwhile. */
    void setWaiting();
    /** Starts it (again). Queued messages are sent once it runs. */
    void start();
    /** Stops it without waiting: stdin is closed, then it is terminated and killed if needed. */
    void stop();
    /** stop() and start(). */
    Q_INVOKABLE void restart();
    /**
     * Sends @p value (anything JSON can hold) as one line. Queued while waiting or starting.
     * @return Whether it was sent or queued: not when there is no backend or it ended.
     */
    Q_INVOKABLE bool send(const QVariant &value);

Q_SIGNALS:
    /** The backend wrote @p value (a JSON line). */
    void message(const QVariant &value);
    void stateChanged();
    /** A line for the tab's log (English). */
    void logLine(const QString &line);

private:
    void setState(State state, int exitCode = 0);
    void readOutput();
    void readErrors();
    void handleLine(const QByteArray &line);
    void flushQueue();
    void write(const QJsonValue &value);
    /** Lets @p process end on its own, then terminates and kills it; it deletes itself. */
    static void reap(QProcess *process);

    WhaleAiTool m_tool;
    QStringList m_command;
    QProcess *m_process = nullptr;
    State m_state = State::None;
    int m_exitCode = 0;
    QByteArray m_output;
    QByteArray m_errors;
    bool m_droppingOutput = false;
    QStringList m_stderrTail;
    QList<QJsonValue> m_queue;
};

#endif
