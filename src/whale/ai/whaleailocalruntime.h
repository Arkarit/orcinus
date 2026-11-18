/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAILOCALRUNTIME_H
#define WHALEAILOCALRUNTIME_H

#include "whaleailocalmodels.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include <functional>
#include <map>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class WhaleAiLocalRuntime;

/**
 * @brief One user of a local model (an agent session of an AI panel).
 *
 * While a lease is held the model stays loaded. Released (or deleted), the model is freed after
 * the grace period if no other lease holds it.
 */
class WhaleAiLocalLease : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Preparing, ///< LM Studio starts or the model loads; see progress()
        Ready,
        Failed, ///< see errorString(); release it
        Released,
    };
    Q_ENUM(State)

    ~WhaleAiLocalLease() override;

    State state() const;
    QString model() const;
    /** @return Why it failed (translated); empty otherwise. */
    QString errorString() const;
    /** @return The instance the agent talks to (LM Studio's identifier); empty until ready. */
    QString instanceId() const;

    /** Gives the model back: freed after the grace period, or at once with @p now. */
    void release(bool now = false);

Q_SIGNALS:
    /** What is going on, for the user (translated), e.g. "Loading Qwen3.8 27B … 12 s". */
    void progress(const QString &text);
    void ready();
    void failed(const QString &error);

private:
    friend class WhaleAiLocalRuntime;
    WhaleAiLocalLease(WhaleAiLocalRuntime *runtime, const QString &model, QObject *parent);
    void setReady(const QString &instanceId);
    void setFailed(const QString &error);

    QPointer<WhaleAiLocalRuntime> m_runtime;
    QString m_model;
    State m_state = State::Preparing;
    QString m_error;
    QString m_instanceId;
};

/**
 * @brief Starts LM Studio and loads models for agents, and frees them again (AI12c).
 *
 * Only what Orcinus started or loaded is stopped or unloaded: a running LM Studio and models
 * the user loaded stay. LM Studio is started as its hidden service (`lms daemon up`, then
 * `lms server start`); models are loaded and unloaded through its REST API. Stopping it takes
 * `lms server stop` and SIGTERM to the service (`lms server stop` alone keeps the models in
 * memory, `lms daemon down` refuses for the app's service).
 *
 * What a process owns is kept in a state file shared by all Orcinus processes. When a process
 * is done with a model another live process uses, it hands it over instead of freeing it; what
 * a crashed process left is freed by cleanUpAfterCrashes().
 *
 * See .privatedoc/ai-support-plan.md (AI12).
 */
class WhaleAiLocalRuntime : public QObject
{
    Q_OBJECT

public:
    struct Options {
        WhaleAiLocalModels::Options discovery;
        QString statePath; ///< empty: <state folder>/orcinus/local-models.json
        qint64 pid = 0; ///< this process in the state file; 0: the real one
        int graceMs = 2 * 60 * 1000; ///< how long a model stays when no lease holds it
        int loadTimeoutMs = 10 * 60 * 1000;
        int commandTimeoutMs = 60 * 1000; ///< for each lms call
        int requestTimeoutMs = 10 * 1000; ///< for each REST call but loading
    };

    explicit WhaleAiLocalRuntime(const Options &options, QObject *parent = nullptr);
    explicit WhaleAiLocalRuntime(QObject *parent = nullptr);
    /** Frees what it owns, like shutdown(). */
    ~WhaleAiLocalRuntime() override;

    /** @return The runtime of the process (frees everything when the application quits). */
    static WhaleAiLocalRuntime *instance();
    /** Makes instance() return @p runtime (nullptr: the real one again); for the tests. */
    static void setInstanceForTests(WhaleAiLocalRuntime *runtime);

    /**
     * @return A lease for @p model of @p provider ("lmstudio"), loaded with @p contextLength
     *         (0: LM Studio's default). It reports ready() or failed() later, never at once.
     */
    WhaleAiLocalLease *acquire(const QString &provider, const QString &model, int contextLength, QObject *parent = nullptr);

    /** Frees everything it owns now, leases or not (the application quits). */
    void shutdown();

    /** Frees what Orcinus processes that are gone (crashed) left; blocks briefly. */
    void cleanUpAfterCrashes();

    int graceMs() const;
    /** @return The state file. */
    QString statePath() const;

Q_SIGNALS:
    /** @p model was unloaded, or handed over to another process. */
    void modelFreed(const QString &model);
    /** LM Studio's service, started by this runtime, was stopped (or handed over). */
    void serviceStopped();

private:
    friend class WhaleAiLocalLease;

    struct Model {
        QString key;
        int contextLength = 0;
        QList<QPointer<WhaleAiLocalLease>> leases;
        bool preparing = false;
        bool ready = false;
        QString instanceId;
        QString displayName;
        QTimer *grace = nullptr;
        QNetworkReply *load = nullptr;
        QTimer *ticker = nullptr; ///< progress while loading
    };

    Model *model(const QString &key) const;
    void releaseLease(WhaleAiLocalLease *lease, bool now);
    /** Makes the model of @p key ready for its leases: checks it, or starts LM Studio and loads it. */
    void check(const QString &key);
    void prepare(Model *model);
    void ensureServer(const std::function<void(const QString &error)> &done);
    void serverStarted(const QString &error);
    void loadModel(const QString &key);
    void finishLoad(const QString &key);
    void modelReady(Model *model, const QString &instanceId);
    void modelFailed(Model *model, const QString &error);
    void progress(const Model *model, const QString &text);
    /** Unloads the model of @p key or hands it over; with @p force also if leases hold it. */
    void freeModel(const QString &key, bool force = false);
    /** @return The loaded instances of @p key at the server, or nothing if it does not answer. */
    std::optional<QStringList> loadedInstances(const QString &key, QString *displayName = nullptr, bool *known = nullptr) const;
    /** Stops LM Studio if this process started it and no longer uses it. */
    void stopServiceIfUnused();

    /** Runs lms with @p arguments; @p done gets the exit code (-1: did not run) and the output. */
    void runLms(const QStringList &arguments, const std::function<void(int exitCode, const QByteArray &output, const QByteArray &errors)> &done);
    /** Runs lms with @p arguments and waits for it. */
    bool runLmsNow(const QString &cli, const QStringList &arguments);

    // The state file. Its entries: {pid, host, port, uses: [model], instances: [{model, id}],
    // loading: [model], servicePid, serverStarted}.
    using StateEdit = std::function<void(QJsonObject &mine, QList<QJsonObject> &others)>;
    /** Changes the state file under its lock; @p edit gets this process' entry and the others. */
    void editState(const StateEdit &edit);
    bool isAlive(qint64 pid) const;
    qint64 pid() const;

    /** What is to be freed once the state file is written. */
    struct Freeing {
        QStringList instances; ///< to unload
        qint64 servicePid = 0; ///< to stop
        bool stopServer = false;
        QString host;
        int port = 0;
    };
    void carryOut(const Freeing &freeing);

    Options m_options;
    std::map<QString, std::unique_ptr<Model>> m_models;
    QNetworkAccessManager *m_network = nullptr;

    // LM Studio as found or started.
    QString m_host;
    int m_port = 0;
    QString m_cli;
    bool m_serverStarting = false;
    QList<std::function<void(const QString &error)>> m_serverWaiting;
};

#endif
