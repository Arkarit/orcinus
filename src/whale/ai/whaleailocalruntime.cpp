/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleailocalruntime.h"
#include "whaleaidebug.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDateTime>
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
const QString Strata = QStringLiteral("strata");
const QString StrataKeyPrefix = QStringLiteral("strata:");

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
        && entry.value(QLatin1String("servicePid")).toInteger() <= 0 && !entry.value(QLatin1String("serverStarted")).toBool()
        && entry.value(QLatin1String("strata")).toObject().isEmpty();
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

WhaleAiLocalLease::WhaleAiLocalLease(WhaleAiLocalRuntime *runtime, const QString &key, const QString &model, QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
    , m_key(key)
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

bool WhaleAiLocalRuntime::processRuns(qint64 pid) const
{
    if (pid <= 0 || ::kill(pid_t(pid), 0) != 0) {
        return false;
    }
    // A server we started goes to PID 1 or a subreaper. Where that does not reap it (e.g. a
    // container without init), it stays a zombie: it has ended all the same. The real /proc, as
    // for kill() (the discovery's may be a test's).
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (stat.open(QIODevice::ReadOnly)) {
        const QByteArray line = stat.readAll();
        const qsizetype end = line.lastIndexOf(')');
        if (end >= 0 && end + 2 < line.size() && line.at(end + 2) == 'Z') {
            return false;
        }
    }
    return true;
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

WhaleAiLocalLease *WhaleAiLocalRuntime::acquire(const QString &provider, const QString &id, int contextLength, QObject *parent)
{
    const QString key = provider == Strata ? StrataKeyPrefix + id : id;
    auto *lease = new WhaleAiLocalLease(this, key, id, parent);
    auto refuse = [lease](const QString &error) {
        QTimer::singleShot(0, lease, [lease, error] {
            lease->setFailed(error);
        });
        return lease;
    };
    if ((provider != LmStudio && provider != Strata) || id.isEmpty()) {
        return refuse(i18nc("@info %1 is a name like lmstudio", "Orcinus cannot start local models of “%1”, only of LM Studio and Strata.", provider));
    }
    // AI13: one local model at a time. LM Studio may hold two of its own models; Strata serves one,
    // and two providers do not fit into memory together.
    for (const auto &[otherKey, other] : m_models) {
        if (otherKey != key && !other->leases.isEmpty() && (other->provider != provider || provider == Strata)) {
            return refuse(i18nc("@info %1 is the name of a language model",
                                "%1 is still in use in another conversation. Only one local model fits into memory at a time: end that "
                                "conversation first.",
                                other->displayName));
        }
    }

    Model *entry = model(key);
    if (!entry) {
        auto created = std::make_unique<Model>();
        created->key = key;
        created->provider = provider;
        created->id = id;
        created->displayName = id;
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
    Model *entry = model(lease->m_key);
    if (!entry || !entry->leases.removeOne(lease)) {
        return;
    }
    entry->leases.removeAll(nullptr);
    if (entry->leases.isEmpty() && now && entry->preparing && entry->provider == Strata && entry->serverPid > 0) {
        // AI13f: a start Orcinus began is broken off at once (the user unloaded it while it loaded).
        freeStrata(entry->key, true);
        return;
    }
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
    if (entry->provider == Strata) {
        // Strata may have unloaded it when idle, or the user may have stopped it.
        if (entry->ready && strataLoadedModel(entry->host, entry->port) == entry->id) {
            modelReady(entry, entry->id);
            return;
        }
        entry->ready = false;
        prepareStrata(entry);
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
    if (entry->provider == Strata) {
        prepareStrata(entry);
        return;
    }
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
    if (key.startsWith(StrataKeyPrefix)) {
        freeStrata(key, force);
        return;
    }
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

// ---- Strata (AI13c) ----

QString WhaleAiLocalRuntime::strataLogPath() const
{
    return QFileInfo(m_options.statePath).absolutePath() + QStringLiteral("/strata-server.log");
}

QString WhaleAiLocalRuntime::strataLoadedModel(const QString &host, int port) const
{
    if (host.isEmpty()) {
        return {};
    }
    const std::optional<QByteArray> body = WhaleAiLocalModels::httpGet(host, port, QStringLiteral("/health"), m_options.requestTimeoutMs);
    const QJsonObject health = body ? QJsonDocument::fromJson(*body).object() : QJsonObject();
    if (health.value(QLatin1String("service")).toString() != Strata || !health.value(QLatin1String("loaded")).toBool()) {
        return {};
    }
    return health.value(QLatin1String("model")).toString();
}

void WhaleAiLocalRuntime::prepareStrata(Model *entry)
{
    entry->preparing = true;
    const WhaleAiLocalSetup setup = WhaleAiLocalModels::discover(m_options.discovery);
    const auto provider = std::find_if(setup.providers.cbegin(), setup.providers.cend(), [](const WhaleAiLocalProvider &provider) {
        return provider.id == Strata;
    });
    if (provider == setup.providers.cend()) {
        modelFailed(entry, i18nc("@info", "Strata was not found."));
        return;
    }
    const auto found = std::find_if(provider->models.cbegin(), provider->models.cend(), [entry](const WhaleAiLocalModel &model) {
        return model.id == entry->id;
    });
    if (found == provider->models.cend()) {
        modelFailed(entry, i18nc("@info %1 is a model id", "Strata has no model “%1”.", entry->id));
        return;
    }
    const WhaleAiLocalModel strataModel = *found;
    entry->displayName = strataModel.name;
    entry->host = provider->host;
    entry->port = provider->port;
    entry->started = QDateTime::currentMSecsSinceEpoch();
    entry->phase.clear();

    if (provider->serverRunning) {
        const std::optional<QByteArray> body = WhaleAiLocalModels::httpGet(entry->host, entry->port, QStringLiteral("/health"), m_options.requestTimeoutMs);
        const QJsonObject health = body ? QJsonDocument::fromJson(*body).object() : QJsonObject();
        const QString running = health.value(QLatin1String("model")).toString();
        if (running != entry->id) {
            QString runningName = running;
            for (const WhaleAiLocalModel &model : provider->models) {
                if (model.id == running) {
                    runningName = model.name;
                }
            }
            modelFailed(entry,
                        i18nc("@info %1 and %2 are model names",
                              "Strata runs %1 at the moment, not %2. Orcinus does not stop a Strata it did not start.",
                              runningName,
                              entry->displayName));
            return;
        }
        if (health.value(QLatin1String("loaded")).toBool()) {
            modelReady(entry, entry->id);
        } else {
            loadStrata(entry);
        }
        return;
    }
    if (provider->appRunning) {
        // A Strata that is starting: it is used if it starts this model.
        if (provider->runningConfig != strataModel.file) {
            modelFailed(entry, i18nc("@info", "Strata is starting with another model at the moment."));
            return;
        }
    } else {
        if (provider->cli.isEmpty()) {
            modelFailed(entry, i18nc("@info", "Strata's Python environment (.venv) was not found; run Strata's setup first."));
            return;
        }
        const QString log = strataLogPath();
        QDir().mkpath(QFileInfo(log).absolutePath());
        QFile::remove(log);
        QProcess process;
        process.setProgram(provider->cli);
        // As Strata's own run scripts, without --open (no browser).
        process.setArguments({provider->folder + QStringLiteral("/serve/server.py"),
                              QStringLiteral("--engine"),
                              QStringLiteral("strata"),
                              QStringLiteral("--config"),
                              strataModel.file,
                              QStringLiteral("--port"),
                              QString::number(entry->port)});
        process.setWorkingDirectory(provider->folder);
        process.setStandardInputFile(QProcess::nullDevice());
        process.setStandardOutputFile(log, QIODevice::Append);
        process.setStandardErrorFile(log, QIODevice::Append);
        qint64 serverPid = 0;
        if (!process.startDetached(&serverPid) || serverPid <= 0) {
            modelFailed(entry, i18nc("@info %1 is an error message", "Strata could not be started: %1", process.errorString()));
            return;
        }
        entry->serverPid = serverPid;
        entry->serverSeen = false;
        entry->logOffset = 0;
        const QString id = entry->id;
        const QString host = entry->host;
        const int port = entry->port;
        editState([serverPid, id, host, port](QJsonObject &mine, QList<QJsonObject> &) {
            mine.insert(
                QStringLiteral("strata"),
                QJsonObject{{QStringLiteral("pid"), serverPid}, {QStringLiteral("model"), id}, {QStringLiteral("host"), host}, {QStringLiteral("port"), port}});
        });
    }

    const QString key = entry->key;
    entry->ticker = new QTimer(this);
    connect(entry->ticker, &QTimer::timeout, this, [this, key] {
        strataTick(key);
    });
    entry->ticker->start(1000);
    strataTick(key);
}

void WhaleAiLocalRuntime::strataTick(const QString &key)
{
    Model *entry = model(key);
    if (!entry || !entry->preparing) {
        return;
    }
    auto finish = [entry] {
        if (entry->ticker) {
            entry->ticker->stop();
            entry->ticker->deleteLater();
            entry->ticker = nullptr;
        }
    };
    auto forget = [this, entry] {
        // The server Orcinus started is gone or given up: the state file no longer names it.
        const qint64 serverPid = std::exchange(entry->serverPid, 0);
        editState([serverPid](QJsonObject &mine, QList<QJsonObject> &) {
            if (mine.value(QLatin1String("strata")).toObject().value(QLatin1String("pid")).toInteger() == serverPid) {
                mine.remove(QStringLiteral("strata"));
            }
        });
    };

    // What it says it does (its own words: AI13a).
    QStringList tail;
    if (entry->serverPid > 0) {
        QFile log(strataLogPath());
        if (log.open(QIODevice::ReadOnly)) {
            log.seek(entry->logOffset);
            const QByteArray read = log.readAll();
            const qsizetype end = read.lastIndexOf('\n') + 1;
            entry->logOffset += end;
            for (const QByteArray &line : read.left(end).split('\n')) {
                if (line.contains("starting the engine")) {
                    entry->phase = i18nc("@info what Strata does", "starting its engine");
                } else if (line.contains("loading the experts")) {
                    entry->phase = i18nc("@info what Strata does", "loading the model into memory");
                } else if (line.contains("experts loaded") || line.contains("filling the GPU")) {
                    entry->phase = i18nc("@info what Strata does", "filling the graphics card's memory");
                } else if (line.contains("almost ready")) {
                    entry->phase = i18nc("@info what Strata does", "almost ready");
                }
            }
        }
    }
    const qint64 seconds = (QDateTime::currentMSecsSinceEpoch() - entry->started) / 1000;
    progress(entry,
             i18nc("@info %1 is a model name, %2 seconds, %3 what Strata does",
                   "Starting %1 … %2 s: %3. The computer may be slow meanwhile.",
                   entry->displayName,
                   seconds,
                   entry->phase.isEmpty() ? i18nc("@info what Strata does", "starting") : entry->phase));

    // Asked without waiting: a server that loads answers late, and a blocking request would stop
    // the GUI (and its busy spinner) for that long. One request at a time; it goes with the ticker.
    if (entry->ticker && !entry->ticker->findChild<QNetworkReply *>()) {
        QNetworkRequest request(QUrl(QStringLiteral("http://%1:%2/health").arg(entry->host).arg(entry->port)));
        request.setTransferTimeout(m_options.requestTimeoutMs);
        QNetworkReply *reply = m_network->get(request);
        reply->setParent(entry->ticker);
        const QPointer<QTimer> ticker = entry->ticker;
        connect(reply, &QNetworkReply::finished, this, [this, key, reply, ticker] {
            reply->deleteLater();
            Model *entry = model(key);
            if (!entry || !entry->preparing || !ticker || entry->ticker != ticker || reply->error() != QNetworkReply::NoError) {
                return;
            }
            const QJsonObject health = QJsonDocument::fromJson(reply->readAll()).object();
            if (health.value(QLatin1String("service")).toString() == Strata && health.value(QLatin1String("loaded")).toBool()
                && health.value(QLatin1String("model")).toString() == entry->id) {
                entry->ticker->stop();
                entry->ticker->deleteLater();
                entry->ticker = nullptr;
                modelReady(entry, entry->id);
            }
        });
    }
    const bool alive = entry->serverPid > 0 ? WhaleAiLocalModels::isStrataServer(m_options.discovery.procPath, entry->serverPid)
                                            : WhaleAiLocalModels::runningStrataConfig(m_options.discovery.procPath).has_value();
    if (alive) {
        entry->serverSeen = true;
    }
    // A process that ended at once (e.g. its port taken) is gone without having been seen.
    const bool gone = entry->serverPid == 0 || entry->serverSeen || !processRuns(entry->serverPid) || seconds >= 10;
    if (!alive && gone) {
        QFile log(strataLogPath());
        if (entry->serverPid > 0 && log.open(QIODevice::ReadOnly)) {
            tail = QString::fromUtf8(log.readAll()).trimmed().split(QLatin1Char('\n'));
            tail = tail.mid(std::max<qsizetype>(0, tail.size() - 8));
        }
        finish();
        forget();
        modelFailed(entry,
                    tail.isEmpty()
                        ? i18nc("@info", "Strata ended while it loaded the model.")
                        : i18nc("@info %1 is the end of Strata's output", "Strata ended while it loaded the model:\n%1", tail.join(QLatin1Char('\n'))));
        if (entry->leases.isEmpty()) {
            entry->grace->start(0);
        }
        return;
    }
    if (seconds * 1000 > m_options.loadTimeoutMs) {
        finish();
        if (entry->serverPid > 0 && WhaleAiLocalModels::isStrataServer(m_options.discovery.procPath, entry->serverPid)) {
            ::kill(pid_t(entry->serverPid), SIGTERM);
        }
        forget();
        modelFailed(entry, i18nc("@info %1 minutes", "Strata did not get ready within %1 minutes.", m_options.loadTimeoutMs / 60000));
        if (entry->leases.isEmpty()) {
            entry->grace->start(0);
        }
    }
}

void WhaleAiLocalRuntime::loadStrata(Model *entry)
{
    // The server is there, its model was unloaded (idle, or by the user): loading it again takes
    // as long as a start (AI13a), and it is unloaded again when Orcinus is done with it.
    QNetworkRequest request(QUrl(QStringLiteral("http://%1:%2/load").arg(entry->host).arg(entry->port)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(m_options.loadTimeoutMs);
    entry->load = m_network->post(request, QByteArrayLiteral("{}"));
    const QString key = entry->key;
    entry->ticker = new QTimer(this);
    auto tick = [this, key] {
        if (Model *entry = model(key)) {
            progress(entry,
                     i18nc("@info %1 is a model name, %2 seconds",
                           "Strata loads %1 again … %2 s. The computer may be slow meanwhile.",
                           entry->displayName,
                           (QDateTime::currentMSecsSinceEpoch() - entry->started) / 1000));
        }
    };
    connect(entry->ticker, &QTimer::timeout, this, tick);
    entry->ticker->start(1000);
    tick();
    connect(entry->load, &QNetworkReply::finished, this, [this, key] {
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
        if (reply->error() != QNetworkReply::NoError) {
            modelFailed(entry, i18nc("@info %1 is an error message", "Strata could not load the model: %1", errorMessage(body, reply->errorString())));
        } else {
            const QString id = entry->id;
            const QString host = entry->host;
            const int port = entry->port;
            editState([id, host, port](QJsonObject &mine, QList<QJsonObject> &) {
                QJsonObject strata = mine.value(QLatin1String("strata")).toObject();
                if (strata.isEmpty()) {
                    strata = {{QStringLiteral("model"), id}, {QStringLiteral("host"), host}, {QStringLiteral("port"), port}};
                }
                strata.insert(QStringLiteral("loadedByUs"), true);
                mine.insert(QStringLiteral("strata"), strata);
            });
            modelReady(entry, entry->id);
        }
        if (entry->leases.isEmpty()) {
            entry->grace->start(entry->ready ? m_options.graceMs : 0);
        }
    });
}

void WhaleAiLocalRuntime::freeStrata(const QString &key, bool force)
{
    Model *entry = model(key);
    if (!entry || (!force && (!entry->leases.isEmpty() || entry->load || entry->preparing))) {
        return;
    }
    if (entry->load) {
        QNetworkReply *reply = std::exchange(entry->load, nullptr);
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (entry->ticker) {
        entry->ticker->stop();
        entry->ticker->deleteLater();
        entry->ticker = nullptr;
    }
    const QList<QPointer<WhaleAiLocalLease>> leases = entry->leases;
    for (const QPointer<WhaleAiLocalLease> &lease : leases) {
        if (lease) {
            lease->setFailed(i18nc("@info", "The local model was stopped."));
        }
    }

    Freeing freeing;
    bool handedOver = false;
    const QString id = entry->id;
    const QString host = entry->host;
    const int port = entry->port;
    // A server that is still loading has no port to unload through yet: it only gets SIGTERM.
    const bool loading = entry->preparing;
    editState([key, id, host, port, loading, &freeing, &handedOver](QJsonObject &mine, QList<QJsonObject> &others) {
        QStringList uses = toStrings(mine.value(QLatin1String("uses")));
        uses.removeAll(key);
        mine.insert(QStringLiteral("uses"), QJsonArray::fromStringList(uses));
        const QJsonObject strata = mine.value(QLatin1String("strata")).toObject();
        if (strata.isEmpty() || strata.value(QLatin1String("model")).toString() != id) {
            return;
        }
        mine.remove(QStringLiteral("strata"));
        // Another Orcinus uses it: it is theirs now.
        const auto other = std::find_if(others.begin(), others.end(), [&key](const QJsonObject &other) {
            return toStrings(other.value(QLatin1String("uses"))).contains(key);
        });
        if (other != others.end()) {
            other->insert(QStringLiteral("strata"), strata);
            handedOver = true;
            return;
        }
        freeing.strataPid = strata.value(QLatin1String("pid")).toInteger();
        freeing.strataUnload = !loading && (freeing.strataPid > 0 || strata.value(QLatin1String("loadedByUs")).toBool());
        freeing.strataHost = strata.value(QLatin1String("host")).toString(host);
        freeing.strataPort = strata.value(QLatin1String("port")).toInt(port);
    });
    if (entry->grace) {
        entry->grace->stop();
        entry->grace->deleteLater();
    }
    m_models.erase(key);
    carryOut(freeing);
    if (freeing.strataUnload || freeing.strataPid > 0 || handedOver) {
        Q_EMIT modelFreed(id);
    }
    stopServiceIfUnused();
}

void WhaleAiLocalRuntime::stopServiceIfUnused()
{
    const bool lmStudioModels = std::any_of(m_models.cbegin(), m_models.cend(), [](const auto &entry) {
        return entry.second->provider == LmStudio;
    });
    if (m_serverStarting || lmStudioModels) {
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
    if (freeing.strataUnload && !freeing.strataHost.isEmpty()) {
        // Waits for a running answer first (there should be none: no conversation holds it).
        const std::optional<WhaleAiLocalModels::HttpReply> reply = WhaleAiLocalModels::httpRequest("POST",
                                                                                                   freeing.strataHost,
                                                                                                   freeing.strataPort,
                                                                                                   QStringLiteral("/unload?wait=1"),
                                                                                                   QByteArrayLiteral("{}"),
                                                                                                   m_options.requestTimeoutMs);
        if (!reply || reply->status / 100 != 2) {
            qCWarning(WhaleAiDebug) << "Strata did not unload its model" << (reply ? reply->body : QByteArray());
        }
    }
    // Only Strata's server: the process may have ended and its id been given to another one.
    if (freeing.strataPid > 0 && WhaleAiLocalModels::isStrataServer(m_options.discovery.procPath, freeing.strataPid)) {
        ::kill(pid_t(freeing.strataPid), SIGTERM);
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
            const QJsonObject strata = entry.value(QLatin1String("strata")).toObject();
            if (!strata.isEmpty()) {
                const QString key = StrataKeyPrefix + strata.value(QLatin1String("model")).toString();
                const auto user = std::find_if(living.begin(), living.end(), [&key](const QJsonObject *living) {
                    return toStrings(living->value(QLatin1String("uses"))).contains(key);
                });
                if (user != living.end()) {
                    (*user)->insert(QStringLiteral("strata"), strata);
                } else {
                    freeing.strataPid = strata.value(QLatin1String("pid")).toInteger();
                    freeing.strataUnload = freeing.strataPid > 0 || strata.value(QLatin1String("loadedByUs")).toBool();
                    freeing.strataHost = strata.value(QLatin1String("host")).toString(QStringLiteral("127.0.0.1"));
                    freeing.strataPort = strata.value(QLatin1String("port")).toInt(8080);
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
