/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleailocalruntime.h"
#include "whaleaidebug.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <signal.h>

namespace
{
const QString LmStudio = QStringLiteral("lmstudio");

QPointer<WhaleAiLocalRuntime> s_instance;
QPointer<WhaleAiLocalRuntime> s_testInstance;

QStringList toStrings(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) {
        result.append(entry.toString());
    }
    return result;
}

bool isEmptyEntry(const QJsonObject &entry)
{
    return entry.value(QLatin1String("uses")).toArray().isEmpty() && entry.value(QLatin1String("instances")).toArray().isEmpty()
        && entry.value(QLatin1String("servicePid")).toInteger() <= 0 && !entry.value(QLatin1String("serverStarted")).toBool();
}

/** @return The error message in a reply of LM Studio's REST API, or @p fallback. */
QString errorMessage(const QByteArray &body, const QString &fallback)
{
    const QString message = QJsonDocument::fromJson(body).object().value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
    return message.isEmpty() ? fallback : message;
}

/** @return The last JSON object line of @p output ("Starting…" lines come before it). */
QJsonObject lastJsonLine(const QByteArray &output)
{
    const QList<QByteArray> lines = output.split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QByteArray line = it->trimmed();
        if (line.startsWith('{')) {
            return QJsonDocument::fromJson(line).object();
        }
    }
    return {};
}
}

// ---- WhaleAiLocalLease ----

WhaleAiLocalLease::WhaleAiLocalLease(WhaleAiLocalRuntime *runtime, const QString &model, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
    , m_model(model)
{
}

WhaleAiLocalLease::~WhaleAiLocalLease()
{
    release();
}

WhaleAiLocalLease::State WhaleAiLocalLease::state() const
{
    return m_state;
}

QString WhaleAiLocalLease::model() const
{
    return m_model;
}

QString WhaleAiLocalLease::errorString() const
{
    return m_error;
}

QString WhaleAiLocalLease::instanceId() const
{
    return m_instanceId;
}

void WhaleAiLocalLease::release(bool now)
{
    if (m_state == State::Released) {
        return;
    }
    m_state = State::Released;
    if (m_runtime) {
        m_runtime->releaseLease(this, now);
    }
}

void WhaleAiLocalLease::setReady(const QString &instanceId)
{
    if (m_state != State::Preparing) {
        return;
    }
    m_state = State::Ready;
    m_instanceId = instanceId;
    Q_EMIT ready();
}

void WhaleAiLocalLease::setFailed(const QString &error)
{
    if (m_state != State::Preparing) {
        return;
    }
    m_state = State::Failed;
    m_error = error;
    Q_EMIT failed(error);
}

// ---- WhaleAiLocalRuntime ----

WhaleAiLocalRuntime::WhaleAiLocalRuntime(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_network(new QNetworkAccessManager(this))
{
    if (m_options.statePath.isEmpty()) {
        QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
        if (folder.isEmpty()) {
            folder = QDir::homePath() + QStringLiteral("/.local/state");
        }
        m_options.statePath = folder + QStringLiteral("/orcinus/local-models.json");
    }
}

WhaleAiLocalRuntime::WhaleAiLocalRuntime(QObject *parent)
    : WhaleAiLocalRuntime(Options(), parent)
{
}

WhaleAiLocalRuntime::~WhaleAiLocalRuntime()
{
    shutdown();
}

WhaleAiLocalRuntime *WhaleAiLocalRuntime::instance()
{
    if (s_testInstance) {
        return s_testInstance;
    }
    if (!s_instance) {
        s_instance = new WhaleAiLocalRuntime(QCoreApplication::instance());
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, s_instance, &WhaleAiLocalRuntime::shutdown);
    }
    return s_instance;
}

void WhaleAiLocalRuntime::setInstanceForTests(WhaleAiLocalRuntime *runtime)
{
    s_testInstance = runtime;
}

int WhaleAiLocalRuntime::graceMs() const
{
    return m_options.graceMs;
}

QString WhaleAiLocalRuntime::statePath() const
{
    return m_options.statePath;
}

qint64 WhaleAiLocalRuntime::pid() const
{
    return m_options.pid > 0 ? m_options.pid : QCoreApplication::applicationPid();
}

bool WhaleAiLocalRuntime::isAlive(qint64 pid) const
{
    return pid > 0 && QFileInfo::exists(QStringLiteral("%1/%2").arg(m_options.discovery.procPath).arg(pid));
}

WhaleAiLocalRuntime::Model *WhaleAiLocalRuntime::model(const QString &key) const
{
    const auto it = m_models.find(key);
    return it == m_models.end() ? nullptr : it->second.get();
}

WhaleAiLocalLease *WhaleAiLocalRuntime::acquire(const QString &provider, const QString &key, int contextLength, QObject *parent)
{
    auto *lease = new WhaleAiLocalLease(this, key, parent);
    if (provider != LmStudio || key.isEmpty()) {
        QTimer::singleShot(0, lease, [lease, provider] {
            lease->setFailed(i18nc("@info %1 is a name like lmstudio", "Orcinus cannot start local models of “%1”, only of LM Studio.", provider));
        });
        return lease;
    }

    Model *entry = model(key);
    if (!entry) {
        auto created = std::make_unique<Model>();
        created->key = key;
        created->displayName = key;
        created->grace = new QTimer(this);
        created->grace->setSingleShot(true);
        connect(created->grace, &QTimer::timeout, this, [this, key] {
            freeModel(key);
        });
        entry = created.get();
        m_models[key] = std::move(created);
    }
    if (!entry->ready && !entry->preparing) {
        entry->contextLength = contextLength;
    }
    entry->grace->stop();
    entry->leases.append(lease);
    // AI12f: one model at a time. Others no conversation holds (in their grace period) go
    // now, before this one takes the graphics card's memory.
    QStringList idle;
    for (const auto &[otherKey, other] : m_models) {
        if (otherKey != key && other->leases.isEmpty() && !other->load && !other->preparing) {
            idle.append(otherKey);
        }
    }
    for (const QString &otherKey : std::as_const(idle)) {
        freeModel(otherKey);
    }
    editState([key](QJsonObject &mine, QList<QJsonObject> &) {
        QStringList uses = toStrings(mine.value(QLatin1String("uses")));
        if (!uses.contains(key)) {
            uses.append(key);
        }
        mine.insert(QStringLiteral("uses"), QJsonArray::fromStringList(uses));
    });
    // Never at once: the caller connects to the lease first.
    QTimer::singleShot(0, this, [this, key] {
        check(key);
    });
    return lease;
}

void WhaleAiLocalRuntime::releaseLease(WhaleAiLocalLease *lease, bool now)
{
    Model *entry = model(lease->model());
    if (!entry || !entry->leases.removeOne(lease)) {
        return;
    }
    entry->leases.removeAll(nullptr);
    if (entry->leases.isEmpty() && !entry->load) {
        // A model that is loading is freed when it is loaded (finishLoad()).
        entry->grace->start(now ? 0 : m_options.graceMs);
    }
}

void WhaleAiLocalRuntime::check(const QString &key)
{
    Model *entry = model(key);
    if (!entry || entry->preparing || entry->leases.isEmpty()) {
        return;
    }
    if (entry->ready) {
        // The user may have unloaded it, or LM Studio may be gone.
        const std::optional<QStringList> instances = loadedInstances(key);
        if (instances && instances->contains(entry->instanceId)) {
            modelReady(entry, entry->instanceId);
            return;
        }
        entry->ready = false;
        entry->instanceId.clear();
    }
    prepare(entry);
}

void WhaleAiLocalRuntime::prepare(Model *entry)
{
    entry->preparing = true;
    const QString key = entry->key;
    ensureServer([this, key](const QString &error) {
        Model *entry = model(key);
        if (!entry) {
            return;
        }
        if (!error.isEmpty()) {
            modelFailed(entry, error);
            return;
        }
        loadModel(key);
    });
}

void WhaleAiLocalRuntime::ensureServer(const std::function<void(const QString &error)> &done)
{
    if (m_serverStarting) {
        m_serverWaiting.append(done);
        return;
    }
    if (!m_host.isEmpty() && WhaleAiLocalModels::httpGet(m_host, m_port, QStringLiteral("/api/v1/models"), m_options.requestTimeoutMs)) {
        done(QString());
        return;
    }

    const WhaleAiLocalSetup setup = WhaleAiLocalModels::discover(m_options.discovery);
    const auto provider = std::find_if(setup.providers.cbegin(), setup.providers.cend(), [](const WhaleAiLocalProvider &provider) {
        return provider.id == LmStudio;
    });
    if (provider == setup.providers.cend()) {
        done(i18nc("@info", "LM Studio is not installed."));
        return;
    }
    m_host = provider->host;
    m_port = provider->port;
    m_cli = provider->cli;
    if (provider->serverRunning) {
        done(QString());
        return;
    }
    if (m_cli.isEmpty()) {
        done(i18nc("@info", "LM Studio's command line tool “lms” was not found, so Orcinus cannot start LM Studio."));
        return;
    }

    m_serverStarting = true;
    m_serverWaiting.append(done);
    for (const auto &[key, entry] : m_models) {
        if (entry->preparing) {
            progress(entry.get(), i18nc("@info", "Starting LM Studio…"));
        }
    }

    auto startServer = [this] {
        runLms({QStringLiteral("server"), QStringLiteral("start")}, [this](int exitCode, const QByteArray &, const QByteArray &errors) {
            if (exitCode != 0) {
                serverStarted(i18nc("@info %1 is an error message", "LM Studio's server did not start: %1", QString::fromLocal8Bit(errors).trimmed()));
                return;
            }
            editState([](QJsonObject &mine, QList<QJsonObject> &) {
                mine.insert(QStringLiteral("serverStarted"), true);
            });
            serverStarted(QString());
        });
    };
    if (provider->appRunning) {
        // The user's LM Studio: only its server is started (and stopped again).
        startServer();
        return;
    }
    runLms({QStringLiteral("daemon"), QStringLiteral("up"), QStringLiteral("--json")},
           [this, startServer](int exitCode, const QByteArray &output, const QByteArray &errors) {
               const qint64 servicePid = lastJsonLine(output).value(QLatin1String("pid")).toInteger();
               if (exitCode != 0 || servicePid <= 0) {
                   serverStarted(i18nc("@info %1 is an error message", "LM Studio did not start: %1", QString::fromLocal8Bit(errors + output).trimmed()));
                   return;
               }
               editState([this, servicePid](QJsonObject &mine, QList<QJsonObject> &) {
                   mine.insert(QStringLiteral("servicePid"), servicePid);
                   mine.insert(QStringLiteral("host"), m_host);
                   mine.insert(QStringLiteral("port"), m_port);
               });
               startServer();
           });
}

void WhaleAiLocalRuntime::serverStarted(const QString &failure)
{
    QString error = failure;
    if (error.isEmpty() && !WhaleAiLocalModels::httpGet(m_host, m_port, QStringLiteral("/api/v1/models"), m_options.requestTimeoutMs)) {
        error =
            i18nc("@info %1 is an address like 127.0.0.1:1234", "LM Studio's server does not answer at %1.", QStringLiteral("%1:%2").arg(m_host).arg(m_port));
    }
    m_serverStarting = false;
    const QList<std::function<void(const QString &)>> waiting = std::exchange(m_serverWaiting, {});
    // On an error, what was started on the way (the service) goes with the failed models.
    for (const auto &done : waiting) {
        done(error);
    }
}

std::optional<QStringList> WhaleAiLocalRuntime::loadedInstances(const QString &key, QString *displayName, bool *known) const
{
    if (m_host.isEmpty()) {
        return std::nullopt;
    }
    const std::optional<QByteArray> body = WhaleAiLocalModels::httpGet(m_host, m_port, QStringLiteral("/api/v1/models"), m_options.requestTimeoutMs);
    if (!body) {
        return std::nullopt;
    }
    QStringList instances;
    const QJsonArray models = QJsonDocument::fromJson(*body).object().value(QLatin1String("models")).toArray();
    for (const QJsonValue &value : models) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QLatin1String("key")).toString() != key) {
            continue;
        }
        if (known) {
            *known = true;
        }
        if (displayName) {
            *displayName = entry.value(QLatin1String("display_name")).toString(key);
        }
        for (const QJsonValue &instance : entry.value(QLatin1String("loaded_instances")).toArray()) {
            instances.append(instance.toObject().value(QLatin1String("id")).toString());
        }
    }
    return instances;
}

void WhaleAiLocalRuntime::loadModel(const QString &key)
{
    Model *entry = model(key);
    bool known = false;
    QString displayName;
    const std::optional<QStringList> instances = loadedInstances(key, &displayName, &known);
    if (!instances) {
        modelFailed(entry, i18nc("@info", "LM Studio's server does not answer."));
        return;
    }
    if (!known) {
        modelFailed(entry, i18nc("@info %1 is a model id", "LM Studio has no model “%1”.", key));
        return;
    }
    entry->displayName = displayName;
    if (!instances->isEmpty()) {
        // Loaded already (by the user, or by Orcinus earlier): used as it is. A second load
        // would load it a second time.
        modelReady(entry, instances->contains(key) ? key : instances->constFirst());
        return;
    }

    QJsonObject body{{QStringLiteral("model"), key}};
    if (entry->contextLength > 0) {
        body.insert(QStringLiteral("context_length"), entry->contextLength);
    }
    QNetworkRequest request(QUrl(QStringLiteral("http://%1:%2/api/v1/models/load").arg(m_host).arg(m_port)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(m_options.loadTimeoutMs);
    entry->load = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(entry->load, &QNetworkReply::finished, this, [this, key] {
        finishLoad(key);
    });

    auto *timer = new QElapsedTimer;
    timer->start();
    entry->ticker = new QTimer(this);
    auto tick = [this, key, timer] {
        if (Model *entry = model(key)) {
            progress(entry, i18nc("@info %1 is a model name, %2 seconds", "Loading %1 … %2 s", entry->displayName, timer->elapsed() / 1000));
        }
    };
    connect(entry->ticker, &QTimer::timeout, this, tick);
    connect(entry->ticker, &QObject::destroyed, this, [timer] {
        delete timer;
    });
    entry->ticker->start(1000);
    tick();
}

void WhaleAiLocalRuntime::finishLoad(const QString &key)
{
    Model *entry = model(key);
    if (!entry || !entry->load) {
        return;
    }
    QNetworkReply *reply = std::exchange(entry->load, nullptr);
    reply->deleteLater();
    if (entry->ticker) {
        entry->ticker->stop();
        entry->ticker->deleteLater();
        entry->ticker = nullptr;
    }
    const QByteArray body = reply->readAll();
    const QString instanceId = QJsonDocument::fromJson(body).object().value(QLatin1String("instance_id")).toString();
    if (reply->error() != QNetworkReply::NoError || instanceId.isEmpty()) {
        modelFailed(entry,
                    i18nc("@info %1 is a model name, %2 an error message",
                          "LM Studio could not load %1: %2",
                          entry->displayName,
                          errorMessage(body, reply->errorString())));
        if (entry->leases.isEmpty()) {
            entry->grace->start(0);
        }
        return;
    }
    editState([key, instanceId](QJsonObject &mine, QList<QJsonObject> &) {
        // An id LM Studio gives again (the user unloaded ours meanwhile) is listed once.
        QJsonArray instances;
        for (const QJsonValue &value : mine.value(QLatin1String("instances")).toArray()) {
            if (value.toObject().value(QLatin1String("id")).toString() != instanceId) {
                instances.append(value);
            }
        }
        instances.append(QJsonObject{{QStringLiteral("model"), key}, {QStringLiteral("id"), instanceId}});
        mine.insert(QStringLiteral("instances"), instances);
    });
    modelReady(entry, instanceId);
}

void WhaleAiLocalRuntime::modelReady(Model *entry, const QString &instanceId)
{
    entry->preparing = false;
    entry->ready = true;
    entry->instanceId = instanceId;
    const QList<QPointer<WhaleAiLocalLease>> leases = entry->leases;
    for (const QPointer<WhaleAiLocalLease> &lease : leases) {
        if (lease) {
            lease->setReady(instanceId);
        }
    }
    if (entry->leases.isEmpty()) {
        entry->grace->start(m_options.graceMs);
    }
}

void WhaleAiLocalRuntime::modelFailed(Model *entry, const QString &error)
{
    entry->preparing = false;
    entry->ready = false;
    const QList<QPointer<WhaleAiLocalLease>> leases = entry->leases;
    for (const QPointer<WhaleAiLocalLease> &lease : leases) {
        if (lease) {
            lease->setFailed(error);
        }
    }
}

void WhaleAiLocalRuntime::progress(const Model *entry, const QString &text)
{
    for (const QPointer<WhaleAiLocalLease> &lease : entry->leases) {
        if (lease && lease->state() == WhaleAiLocalLease::State::Preparing) {
            Q_EMIT lease->progress(text);
        }
    }
}

void WhaleAiLocalRuntime::freeModel(const QString &key, bool force)
{
    Model *entry = model(key);
    if (!entry || (!force && (!entry->leases.isEmpty() || entry->load || entry->preparing))) {
        return;
    }
    if (entry->load) {
        // Quitting while it loads: what loads is gone with the service if it is ours.
        QNetworkReply *reply = std::exchange(entry->load, nullptr);
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    const QList<QPointer<WhaleAiLocalLease>> leases = entry->leases;
    for (const QPointer<WhaleAiLocalLease> &lease : leases) {
        if (lease) {
            lease->setFailed(i18nc("@info", "The local model was stopped."));
        }
    }

    Freeing freeing;
    freeing.host = m_host;
    freeing.port = m_port;
    bool handedOver = false;
    editState([key, &freeing, &handedOver](QJsonObject &mine, QList<QJsonObject> &others) {
        QStringList uses = toStrings(mine.value(QLatin1String("uses")));
        uses.removeAll(key);
        mine.insert(QStringLiteral("uses"), QJsonArray::fromStringList(uses));
        QJsonArray kept;
        for (const QJsonValue &value : mine.value(QLatin1String("instances")).toArray()) {
            const QJsonObject instance = value.toObject();
            if (instance.value(QLatin1String("model")).toString() != key) {
                kept.append(instance);
                continue;
            }
            // Another Orcinus uses it: it is theirs now.
            const auto other = std::find_if(others.begin(), others.end(), [&key](const QJsonObject &other) {
                return toStrings(other.value(QLatin1String("uses"))).contains(key);
            });
            if (other != others.end()) {
                QJsonArray theirs = other->value(QLatin1String("instances")).toArray();
                theirs.append(instance);
                other->insert(QStringLiteral("instances"), theirs);
                handedOver = true;
            } else {
                freeing.instances.append(instance.value(QLatin1String("id")).toString());
            }
        }
        mine.insert(QStringLiteral("instances"), kept);
    });
    if (entry->grace) {
        entry->grace->stop();
        entry->grace->deleteLater();
    }
    if (entry->ticker) {
        entry->ticker->deleteLater();
    }
    m_models.erase(key);
    carryOut(freeing);
    if (!freeing.instances.isEmpty() || handedOver) {
        Q_EMIT modelFreed(key);
    }
    stopServiceIfUnused();
}

void WhaleAiLocalRuntime::stopServiceIfUnused()
{
    if (m_serverStarting || !m_models.empty()) {
        return;
    }
    Freeing freeing;
    freeing.host = m_host;
    freeing.port = m_port;
    bool handedOver = false;
    editState([this, &freeing, &handedOver](QJsonObject &mine, QList<QJsonObject> &others) {
        const qint64 servicePid = mine.value(QLatin1String("servicePid")).toInteger();
        const bool serverStarted = mine.value(QLatin1String("serverStarted")).toBool();
        if (servicePid <= 0 && !serverStarted) {
            return;
        }
        mine.remove(QStringLiteral("servicePid"));
        mine.remove(QStringLiteral("serverStarted"));
        // Another Orcinus that is alive may be using it (it found it running).
        const auto other = std::find_if(others.begin(), others.end(), [this](const QJsonObject &other) {
            return isAlive(other.value(QLatin1String("pid")).toInteger());
        });
        if (other != others.end()) {
            if (servicePid > 0) {
                other->insert(QStringLiteral("servicePid"), servicePid);
            }
            if (serverStarted) {
                other->insert(QStringLiteral("serverStarted"), true);
            }
            handedOver = true;
            return;
        }
        freeing.servicePid = servicePid;
        freeing.stopServer = serverStarted;
    });
    carryOut(freeing);
    if (handedOver || freeing.servicePid > 0 || freeing.stopServer) {
        Q_EMIT serviceStopped();
    }
}

void WhaleAiLocalRuntime::carryOut(const Freeing &freeing)
{
    for (const QString &id : freeing.instances) {
        const std::optional<WhaleAiLocalModels::HttpReply> reply =
            WhaleAiLocalModels::httpRequest("POST",
                                            freeing.host,
                                            freeing.port,
                                            QStringLiteral("/api/v1/models/unload"),
                                            QJsonDocument(QJsonObject{{QStringLiteral("instance_id"), id}}).toJson(QJsonDocument::Compact),
                                            m_options.requestTimeoutMs);
        if (!reply || reply->status / 100 != 2) {
            qCWarning(WhaleAiDebug) << "Could not unload" << id << (reply ? reply->body : QByteArray());
        }
    }
    if (freeing.stopServer || freeing.servicePid > 0) {
        QString cli = m_cli;
        if (cli.isEmpty()) {
            for (const WhaleAiLocalProvider &provider : WhaleAiLocalModels::discover(m_options.discovery).providers) {
                if (provider.id == LmStudio) {
                    cli = provider.cli;
                }
            }
        }
        if (freeing.stopServer && !cli.isEmpty()) {
            runLmsNow(cli, {QStringLiteral("server"), QStringLiteral("stop")});
        }
        // Only LM Studio: the process may have ended and its id been given to another one.
        if (freeing.servicePid > 0 && WhaleAiLocalModels::isLmStudioProcess(m_options.discovery.procPath, freeing.servicePid)) {
            ::kill(pid_t(freeing.servicePid), SIGTERM);
        }
    }
}

void WhaleAiLocalRuntime::shutdown()
{
    QStringList keys;
    for (const auto &[key, entry] : m_models) {
        keys.append(key);
    }
    for (const QString &key : std::as_const(keys)) {
        freeModel(key, true);
    }
    m_serverStarting = false;
    m_serverWaiting.clear();
    stopServiceIfUnused();
}

void WhaleAiLocalRuntime::cleanUpAfterCrashes()
{
    if (!QFile::exists(m_options.statePath)) {
        return;
    }
    // Where LM Studio is, for entries that do not say.
    QString host = m_host;
    int port = m_port;
    if (host.isEmpty()) {
        for (const WhaleAiLocalProvider &provider : WhaleAiLocalModels::discover(m_options.discovery).providers) {
            if (provider.id == LmStudio) {
                host = provider.host;
                port = provider.port;
            }
        }
    }
    QList<Freeing> freeings;
    editState([this, &freeings, host, port](QJsonObject &mine, QList<QJsonObject> &others) {
        QList<QJsonObject *> living{&mine};
        QList<QJsonObject> gone;
        QList<QJsonObject> kept;
        for (const QJsonObject &other : std::as_const(others)) {
            if (isAlive(other.value(QLatin1String("pid")).toInteger())) {
                kept.append(other);
            } else {
                gone.append(other);
            }
        }
        others = kept;
        for (QJsonObject &other : others) {
            living.append(&other);
        }
        for (const QJsonObject &entry : std::as_const(gone)) {
            Freeing freeing;
            freeing.host = entry.value(QLatin1String("host")).toString(host);
            freeing.port = entry.value(QLatin1String("port")).toInt(port);
            for (const QJsonValue &value : entry.value(QLatin1String("instances")).toArray()) {
                const QJsonObject instance = value.toObject();
                const QString key = instance.value(QLatin1String("model")).toString();
                const auto user = std::find_if(living.begin(), living.end(), [&key](const QJsonObject *living) {
                    return toStrings(living->value(QLatin1String("uses"))).contains(key);
                });
                if (user != living.end()) {
                    QJsonArray theirs = (*user)->value(QLatin1String("instances")).toArray();
                    theirs.append(instance);
                    (*user)->insert(QStringLiteral("instances"), theirs);
                } else {
                    freeing.instances.append(instance.value(QLatin1String("id")).toString());
                }
            }
            const qint64 servicePid = entry.value(QLatin1String("servicePid")).toInteger();
            const bool serverStarted = entry.value(QLatin1String("serverStarted")).toBool();
            const auto user = std::find_if(living.begin(), living.end(), [](const QJsonObject *living) {
                return !isEmptyEntry(*living);
            });
            if (user != living.end()) {
                if (servicePid > 0) {
                    (*user)->insert(QStringLiteral("servicePid"), servicePid);
                }
                if (serverStarted) {
                    (*user)->insert(QStringLiteral("serverStarted"), true);
                }
            } else {
                freeing.servicePid = servicePid;
                freeing.stopServer = serverStarted;
            }
            freeings.append(freeing);
        }
    });
    for (const Freeing &freeing : std::as_const(freeings)) {
        carryOut(freeing);
    }
}

void WhaleAiLocalRuntime::editState(const StateEdit &edit)
{
    const QString path = m_options.statePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(10000);
    if (!lock.tryLock(5000)) {
        qCWarning(WhaleAiDebug) << "The state file of the local models is locked; changed anyway:" << path;
    }

    QList<QJsonObject> others;
    QJsonObject mine{{QStringLiteral("pid"), pid()}};
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonArray entries = QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("entries")).toArray();
        for (const QJsonValue &value : entries) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QLatin1String("pid")).toInteger() == pid()) {
                mine = entry;
            } else if (!isEmptyEntry(entry)) {
                others.append(entry);
            }
        }
        file.close();
    }

    edit(mine, others);

    QJsonArray entries;
    for (const QJsonObject &other : std::as_const(others)) {
        if (!isEmptyEntry(other)) {
            entries.append(other);
        }
    }
    if (!isEmptyEntry(mine)) {
        if (!m_host.isEmpty()) {
            mine.insert(QStringLiteral("host"), m_host);
            mine.insert(QStringLiteral("port"), m_port);
        }
        entries.append(mine);
    }
    if (entries.isEmpty()) {
        QFile::remove(path);
        return;
    }
    QSaveFile save(path);
    if (!save.open(QIODevice::WriteOnly)) {
        qCWarning(WhaleAiDebug) << "Cannot write the state file of the local models:" << path << save.errorString();
        return;
    }
    save.write(QJsonDocument(QJsonObject{{QStringLiteral("entries"), entries}}).toJson());
    save.commit();
}

void WhaleAiLocalRuntime::runLms(const QStringList &arguments, const std::function<void(int, const QByteArray &, const QByteArray &)> &done)
{
    auto *process = new QProcess(this);
    auto finished = std::make_shared<bool>(false);
    auto finish = [process, done, finished](int exitCode) {
        if (*finished) {
            return;
        }
        *finished = true;
        done(exitCode, process->readAllStandardOutput(), process->readAllStandardError());
        process->deleteLater();
    };
    connect(process, &QProcess::finished, this, [process, finish](int exitCode, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit ? exitCode : -1);
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(-1);
        }
    });
    QTimer::singleShot(m_options.commandTimeoutMs, process, [process] {
        process->kill();
    });
    process->start(m_cli, arguments);
}

bool WhaleAiLocalRuntime::runLmsNow(const QString &cli, const QStringList &arguments)
{
    QProcess process;
    process.start(cli, arguments);
    if (!process.waitForFinished(m_options.commandTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

#include "moc_whaleailocalruntime.cpp"
