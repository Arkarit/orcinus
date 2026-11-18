/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAIFAKELMSTUDIO_H
#define WHALEAIFAKELMSTUDIO_H

#include "whaleailocalruntime.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <signal.h>

/**
 * @brief LM Studio for the tests (AI12c): its REST API (models, load, unload) in a thread of its
 *        own, and an "lms" script that starts a dummy "lm-studio" service and switches the server
 *        on and off. Everything lives in a temporary folder; /proc is a fake one in it.
 *
 * The server answers only while the file "up" exists (else it closes the connection, like a
 * server that is not there). The script logs its calls to "lms.log".
 */
class FakeLmStudio : public QThread
{
public:
    FakeLmStudio()
    {
        QDir().mkpath(procPath());
        QDir().mkpath(home() + QStringLiteral("/.lmstudio/.internal"));
        start();
        m_ready.acquire();
        writeFile(home() + QStringLiteral("/.lmstudio/.internal/http-server-config.json"),
                  QStringLiteral(R"({"port": %1, "networkInterface": "127.0.0.1", "justInTimeModelLoading": true})").arg(port()).toUtf8());
        const QString lms = home() + QStringLiteral("/.lmstudio/bin/lms");
        writeFile(lms,
                  "#!/bin/bash\n"
                  "D=\"$FAKE_LMS_DIR\"\n"
                  "echo \"$*\" >> \"$D/lms.log\"\n"
                  "case \"$1 $2\" in\n"
                  "  \"daemon up\")\n"
                  "    [ -f \"$D/fail-daemon\" ] && { echo 'no service' >&2; exit 1; }\n"
                  "    (exec -a lm-studio sleep 600) >/dev/null 2>&1 &\n"
                  "    pid=$!\n"
                  "    mkdir -p \"$D/proc/$pid\"; printf 'lm-studio\\0--run-as-service\\0' > \"$D/proc/$pid/cmdline\"\n"
                  "    echo \"$pid\" > \"$D/service.pid\"\n"
                  "    echo 'Starting local model service...'\n"
                  "    echo \"{\\\"status\\\":\\\"running\\\",\\\"pid\\\":$pid,\\\"isDaemon\\\":false}\";;\n"
                  "  \"server start\")\n"
                  "    [ -f \"$D/fail-server\" ] && { echo 'port 1234 is busy' >&2; exit 1; }\n"
                  "    touch \"$D/up\";;\n"
                  "  \"server stop\") rm -f \"$D/up\";;\n"
                  "esac\n");
        QFile::setPermissions(lms, QFile::permissions(lms) | QFile::ExeOwner);
        qputenv("FAKE_LMS_DIR", m_dir.path().toLocal8Bit());
    }

    ~FakeLmStudio() override
    {
        m_stop = true;
        wait();
        const qint64 service = servicePid();
        if (service > 0) {
            ::kill(pid_t(service), SIGKILL);
        }
    }

    QString path() const
    {
        return m_dir.path();
    }
    QString home() const
    {
        return m_dir.filePath(QStringLiteral("home"));
    }
    QString procPath() const
    {
        return m_dir.filePath(QStringLiteral("proc"));
    }
    quint16 port() const
    {
        return m_port;
    }

    /** Options for a runtime that only sees this LM Studio. */
    WhaleAiLocalRuntime::Options options(int graceMs = 200) const
    {
        WhaleAiLocalRuntime::Options options;
        options.discovery.homePath = home();
        options.discovery.searchPaths = {m_dir.filePath(QStringLiteral("nobin"))};
        options.discovery.procPath = procPath();
        options.discovery.probeTimeoutMs = 2000;
        options.statePath = m_dir.filePath(QStringLiteral("state/local-models.json"));
        options.pid = 4000000; // not in the fake /proc, like any process that is not looked at
        options.graceMs = graceMs;
        options.commandTimeoutMs = 10000;
        options.requestTimeoutMs = 2000;
        return options;
    }

    void addModel(const QString &key, const QString &name)
    {
        QMutexLocker locker(&m_mutex);
        m_models.insert(key, name);
    }
    void setLoaded(const QString &key, const QStringList &instances)
    {
        QMutexLocker locker(&m_mutex);
        m_instances.insert(key, instances);
    }
    QStringList loaded(const QString &key) const
    {
        QMutexLocker locker(&m_mutex);
        return m_instances.value(key);
    }
    void setLoadDelay(int ms)
    {
        m_loadDelayMs = ms;
    }
    /** @return The requests the REST API got: "POST /api/v1/models/load {…}". */
    QStringList requests() const
    {
        QMutexLocker locker(&m_mutex);
        return m_requests;
    }
    QStringList lmsCalls() const
    {
        QFile log(m_dir.filePath(QStringLiteral("lms.log")));
        if (!log.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    void setServerUp(bool up)
    {
        if (up) {
            writeFile(m_dir.filePath(QStringLiteral("up")), "");
        } else {
            QFile::remove(m_dir.filePath(QStringLiteral("up")));
        }
    }
    bool serverUp() const
    {
        return QFile::exists(m_dir.filePath(QStringLiteral("up")));
    }
    /** Makes "lms daemon up" or "lms server start" fail. */
    void setFailing(const QString &what)
    {
        writeFile(m_dir.filePath(QStringLiteral("fail-") + what), "");
    }
    /** A process in the fake /proc, e.g. the user's own LM Studio or another Orcinus. */
    void addProcess(qint64 pid, const QByteArray &program)
    {
        writeFile(QStringLiteral("%1/%2/cmdline").arg(procPath()).arg(pid), program + '\0');
    }
    /** @return The dummy service "lms daemon up" started; 0 if none. */
    qint64 servicePid() const
    {
        QFile file(m_dir.filePath(QStringLiteral("service.pid")));
        return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toLongLong() : 0;
    }
    static bool isRunning(qint64 pid)
    {
        // Our dummy service is reaped by init, so it does not linger as a zombie.
        return pid > 0 && ::kill(pid_t(pid), 0) == 0;
    }

    static void writeFile(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(content);
        }
    }

protected:
    void run() override
    {
        QTcpServer server;
        server.listen(QHostAddress::LocalHost);
        m_port = server.serverPort();
        m_ready.release();
        while (!m_stop) {
            if (!server.waitForNewConnection(20)) {
                continue;
            }
            QTcpSocket *socket = server.nextPendingConnection();
            if (serverUp()) {
                serve(socket);
            }
            socket->disconnectFromHost();
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                socket->waitForDisconnected(1000);
            }
            delete socket;
        }
    }

private:
    void serve(QTcpSocket *socket)
    {
        QByteArray request;
        qsizetype headerEnd = -1;
        while ((headerEnd = request.indexOf("\r\n\r\n")) < 0 && socket->waitForReadyRead(2000)) {
            request.append(socket->readAll());
        }
        if (headerEnd < 0) {
            return;
        }
        qsizetype length = 0;
        for (const QByteArray &line : request.left(headerEnd).split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
                length = line.mid(15).trimmed().toLongLong();
            }
        }
        while (request.size() < headerEnd + 4 + length && socket->waitForReadyRead(2000)) {
            request.append(socket->readAll());
        }
        const QList<QByteArray> first = request.left(request.indexOf("\r\n")).split(' ');
        const QByteArray method = first.value(0);
        const QByteArray target = first.value(1);
        const QByteArray body = request.mid(headerEnd + 4, length);
        {
            QMutexLocker locker(&m_mutex);
            m_requests.append(QString::fromUtf8(method + ' ' + target + (body.isEmpty() ? QByteArray() : ' ' + body)));
        }

        int status = 200;
        QJsonObject reply;
        const QJsonObject arguments = QJsonDocument::fromJson(body).object();
        if (method == "GET" && target == "/api/v1/models") {
            QMutexLocker locker(&m_mutex);
            QJsonArray models;
            for (auto it = m_models.cbegin(); it != m_models.cend(); ++it) {
                QJsonArray instances;
                for (const QString &id : m_instances.value(it.key())) {
                    instances.append(QJsonObject{{QStringLiteral("id"), id}});
                }
                models.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("llm")},
                                          {QStringLiteral("key"), it.key()},
                                          {QStringLiteral("display_name"), it.value()},
                                          {QStringLiteral("loaded_instances"), instances}});
            }
            reply.insert(QStringLiteral("models"), models);
        } else if (method == "POST" && target == "/api/v1/models/load") {
            QThread::msleep(m_loadDelayMs);
            QMutexLocker locker(&m_mutex);
            const QString key = arguments.value(QLatin1String("model")).toString();
            if (!m_models.contains(key)) {
                status = 400;
                reply.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), QString(QStringLiteral("Model not found: ") + key)}});
            } else {
                QStringList &instances = m_instances[key];
                const QString id = instances.isEmpty() ? key : QStringLiteral("%1:%2").arg(key).arg(instances.size() + 1);
                instances.append(id);
                reply = {{QStringLiteral("instance_id"), id}, {QStringLiteral("status"), QStringLiteral("loaded")}};
            }
        } else if (method == "POST" && target == "/api/v1/models/unload") {
            QMutexLocker locker(&m_mutex);
            const QString id = arguments.value(QLatin1String("instance_id")).toString();
            status = 404;
            for (QStringList &instances : m_instances) {
                if (instances.removeOne(id)) {
                    status = 200;
                }
            }
            reply.insert(QStringLiteral("instance_id"), id);
        } else {
            status = 404;
        }
        const QByteArray json = QJsonDocument(reply).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error")
                      + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json);
        socket->waitForBytesWritten(2000);
    }

    QTemporaryDir m_dir;
    QSemaphore m_ready;
    std::atomic<quint16> m_port = 0;
    std::atomic<bool> m_stop = false;
    std::atomic<int> m_loadDelayMs = 0;
    mutable QMutex m_mutex;
    QMap<QString, QString> m_models;
    QMap<QString, QStringList> m_instances;
    QStringList m_requests;
};

#endif
