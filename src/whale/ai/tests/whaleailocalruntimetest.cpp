/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleailocalruntime.h"
#include "whaleaifakelmstudio.h"

#include <QSignalSpy>
#include <QTest>

namespace
{
QJsonArray stateEntries(const WhaleAiLocalRuntime &runtime)
{
    QFile file(runtime.statePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("entries")).toArray();
}

QStringList matching(const QStringList &lines, const QString &start)
{
    QStringList result;
    for (const QString &line : lines) {
        if (line.startsWith(start)) {
            result.append(line);
        }
    }
    return result;
}
}

class WhaleAiLocalRuntimeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsAndStopsLmStudio();
    void leavesTheUsersLmStudio();
    void startsOnlyTheServer();
    void gracePeriod();
    void failures();
    void handsOverToAnotherOrcinus();
    void cleansUpAfterCrash();
    void shutdownWhileLoading();
    void otherProvider();
};

void WhaleAiLocalRuntimeTest::startsAndStopsLmStudio()
{
    // Nothing runs: Orcinus starts the service and the server, loads, and stops it all again.
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    lmStudio.setLoadDelay(1200);
    WhaleAiLocalRuntime runtime(lmStudio.options());

    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 4096, this);
    QSignalSpy progress(lease, &WhaleAiLocalLease::progress);
    QSignalSpy ready(lease, &WhaleAiLocalLease::ready);
    QCOMPARE(lease->state(), WhaleAiLocalLease::State::Preparing);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(lease->state(), WhaleAiLocalLease::State::Ready);
    QCOMPARE(lease->instanceId(), QStringLiteral("qwen"));
    QCOMPARE(progress.first().first().toString(), QStringLiteral("Starting LM Studio…"));
    QVERIFY2(progress.last().first().toString().startsWith(QStringLiteral("Loading Qwen 27B … ")), qPrintable(progress.last().first().toString()));
    QCOMPARE(lmStudio.lmsCalls(), (QStringList{QStringLiteral("daemon up --json"), QStringLiteral("server start")}));
    const QStringList loads = matching(lmStudio.requests(), QStringLiteral("POST /api/v1/models/load"));
    QCOMPARE(loads.size(), 1);
    QVERIFY(loads.first().contains(QLatin1String("\"context_length\":4096")));
    const qint64 service = lmStudio.servicePid();
    QVERIFY(FakeLmStudio::isRunning(service));

    QJsonArray entries = stateEntries(runtime);
    QCOMPARE(entries.size(), 1);
    QJsonObject mine = entries.first().toObject();
    QCOMPARE(mine.value(QLatin1String("servicePid")).toInteger(), service);
    QVERIFY(mine.value(QLatin1String("serverStarted")).toBool());
    QCOMPARE(mine.value(QLatin1String("port")).toInt(), int(lmStudio.port()));
    QCOMPARE(mine.value(QLatin1String("instances")).toArray().first().toObject().value(QLatin1String("id")).toString(), QStringLiteral("qwen"));

    QSignalSpy freed(&runtime, &WhaleAiLocalRuntime::modelFreed);
    QSignalSpy stopped(&runtime, &WhaleAiLocalRuntime::serviceStopped);
    lease->release(true);
    QTRY_COMPARE(freed.count(), 1);
    QCOMPARE(stopped.count(), 1);
    QVERIFY(lmStudio.loaded(QStringLiteral("qwen")).isEmpty());
    QCOMPARE(lmStudio.lmsCalls().last(), QStringLiteral("server stop"));
    QVERIFY(!lmStudio.serverUp());
    QTRY_VERIFY(!FakeLmStudio::isRunning(service));
    QVERIFY(!QFile::exists(runtime.statePath()));
    delete lease;
}

void WhaleAiLocalRuntimeTest::leavesTheUsersLmStudio()
{
    // The user's LM Studio with the model loaded: used as it is, nothing loaded, unloaded or stopped.
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    lmStudio.setLoaded(QStringLiteral("qwen"), {QStringLiteral("qwen")});
    lmStudio.addProcess(777, "/tmp/.mount_x/lm-studio");
    lmStudio.setServerUp(true);
    WhaleAiLocalRuntime runtime(lmStudio.options());

    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QSignalSpy ready(lease, &WhaleAiLocalLease::ready);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(lease->instanceId(), QStringLiteral("qwen"));
    QVERIFY(lmStudio.lmsCalls().isEmpty());
    QVERIFY(matching(lmStudio.requests(), QStringLiteral("POST")).isEmpty());

    QSignalSpy freed(&runtime, &WhaleAiLocalRuntime::modelFreed);
    QSignalSpy stopped(&runtime, &WhaleAiLocalRuntime::serviceStopped);
    lease->release(true);
    QTest::qWait(300);
    QCOMPARE(freed.count(), 0);
    QCOMPARE(stopped.count(), 0);
    QCOMPARE(lmStudio.loaded(QStringLiteral("qwen")), QStringList{QStringLiteral("qwen")});
    QVERIFY(lmStudio.serverUp());
    QVERIFY(lmStudio.lmsCalls().isEmpty());
    QVERIFY(!QFile::exists(runtime.statePath()));
    delete lease;
}

void WhaleAiLocalRuntimeTest::startsOnlyTheServer()
{
    // The user's LM Studio runs without its server: only the server is started and stopped.
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    lmStudio.addProcess(777, "/home/u/LM-Studio-0.4.28-2-linux-x64.AppImage");
    WhaleAiLocalRuntime runtime(lmStudio.options());

    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QSignalSpy ready(lease, &WhaleAiLocalLease::ready);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(lmStudio.lmsCalls(), QStringList{QStringLiteral("server start")});
    QVERIFY(!matching(lmStudio.requests(), QStringLiteral("POST /api/v1/models/load")).first().contains(QLatin1String("context_length")));

    QSignalSpy stopped(&runtime, &WhaleAiLocalRuntime::serviceStopped);
    lease->release(true);
    QTRY_COMPARE(stopped.count(), 1);
    QCOMPARE(lmStudio.lmsCalls(), (QStringList{QStringLiteral("server start"), QStringLiteral("server stop")}));
    QVERIFY(lmStudio.loaded(QStringLiteral("qwen")).isEmpty());
    delete lease;
}

void WhaleAiLocalRuntimeTest::gracePeriod()
{
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    lmStudio.setServerUp(true);
    lmStudio.addProcess(777, "lm-studio");
    WhaleAiLocalRuntime runtime(lmStudio.options(400));
    QSignalSpy freed(&runtime, &WhaleAiLocalRuntime::modelFreed);

    // Two sessions share one load.
    WhaleAiLocalLease *first = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    WhaleAiLocalLease *second = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QTRY_COMPARE(first->state(), WhaleAiLocalLease::State::Ready);
    QTRY_COMPARE(second->state(), WhaleAiLocalLease::State::Ready);
    QCOMPARE(matching(lmStudio.requests(), QStringLiteral("POST /api/v1/models/load")).size(), 1);

    // One gone: it stays, also after the grace period.
    delete first;
    QTest::qWait(600);
    QCOMPARE(freed.count(), 0);

    // The last one gone, and a new one within the grace period: still the same instance.
    second->release();
    QTest::qWait(150);
    WhaleAiLocalLease *third = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QTRY_COMPARE(third->state(), WhaleAiLocalLease::State::Ready);
    QTest::qWait(500);
    QCOMPARE(freed.count(), 0);
    QCOMPARE(matching(lmStudio.requests(), QStringLiteral("POST /api/v1/models/load")).size(), 1);
    QCOMPARE(lmStudio.loaded(QStringLiteral("qwen")), QStringList{QStringLiteral("qwen")});

    // The user unloaded it meanwhile: a new lease loads it again.
    lmStudio.setLoaded(QStringLiteral("qwen"), {});
    WhaleAiLocalLease *fourth = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QTRY_COMPARE(fourth->state(), WhaleAiLocalLease::State::Ready);
    QCOMPARE(matching(lmStudio.requests(), QStringLiteral("POST /api/v1/models/load")).size(), 2);

    // All gone: freed after the grace period, not before.
    delete third;
    delete fourth;
    QTest::qWait(200);
    QCOMPARE(freed.count(), 0);
    QTRY_COMPARE(freed.count(), 1);
    QVERIFY(lmStudio.loaded(QStringLiteral("qwen")).isEmpty());
    delete second;
}

void WhaleAiLocalRuntimeTest::failures()
{
    {
        // A model LM Studio does not have; the service started for it goes again.
        FakeLmStudio lmStudio;
        lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
        WhaleAiLocalRuntime runtime(lmStudio.options());
        WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("nope"), 0, this);
        QSignalSpy failed(lease, &WhaleAiLocalLease::failed);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(lease->errorString(), QStringLiteral("LM Studio has no model “nope”."));
        const qint64 service = lmStudio.servicePid();
        QVERIFY(FakeLmStudio::isRunning(service));
        lease->release(true);
        QTRY_VERIFY(!FakeLmStudio::isRunning(service));
        QVERIFY(!lmStudio.serverUp());
        delete lease;
    }
    {
        // The server does not start.
        FakeLmStudio lmStudio;
        lmStudio.setFailing(QStringLiteral("server"));
        WhaleAiLocalRuntime runtime(lmStudio.options());
        WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
        QSignalSpy failed(lease, &WhaleAiLocalLease::failed);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
        QCOMPARE(lease->errorString(), QStringLiteral("LM Studio's server did not start: port 1234 is busy"));
        const qint64 service = lmStudio.servicePid();
        delete lease;
        QTRY_VERIFY(!FakeLmStudio::isRunning(service));
        QVERIFY(!QFile::exists(runtime.statePath()));
    }
    {
        // Not installed.
        FakeLmStudio lmStudio;
        WhaleAiLocalRuntime::Options options = lmStudio.options();
        options.discovery.homePath = lmStudio.path() + QStringLiteral("/empty");
        WhaleAiLocalRuntime runtime(options);
        WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
        QSignalSpy failed(lease, &WhaleAiLocalLease::failed);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(lease->errorString(), QStringLiteral("LM Studio is not installed."));
        delete lease;
    }
}

void WhaleAiLocalRuntimeTest::handsOverToAnotherOrcinus()
{
    // Another Orcinus (alive) uses the same model: we started and loaded it, it gets both.
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    WhaleAiLocalRuntime runtime(lmStudio.options());
    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QTRY_COMPARE(lease->state(), WhaleAiLocalLease::State::Ready);
    const qint64 service = lmStudio.servicePid();

    WhaleAiLocalRuntime::Options otherOptions = lmStudio.options();
    otherOptions.pid = 5555;
    lmStudio.addProcess(5555, "orcinus");
    WhaleAiLocalRuntime other(otherOptions);
    WhaleAiLocalLease *otherLease = other.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QTRY_COMPARE(otherLease->state(), WhaleAiLocalLease::State::Ready);

    QSignalSpy freed(&runtime, &WhaleAiLocalRuntime::modelFreed);
    lease->release(true);
    QTRY_COMPARE(freed.count(), 1);
    QCOMPARE(lmStudio.loaded(QStringLiteral("qwen")), QStringList{QStringLiteral("qwen")});
    QVERIFY(FakeLmStudio::isRunning(service));
    QVERIFY(lmStudio.serverUp());
    const QJsonArray entries = stateEntries(runtime);
    QCOMPARE(entries.size(), 1);
    const QJsonObject theirs = entries.first().toObject();
    QCOMPARE(theirs.value(QLatin1String("pid")).toInteger(), 5555);
    QCOMPARE(theirs.value(QLatin1String("servicePid")).toInteger(), service);
    QCOMPARE(theirs.value(QLatin1String("instances")).toArray().size(), 1);

    // The other one is done: now it all goes.
    QSignalSpy stopped(&other, &WhaleAiLocalRuntime::serviceStopped);
    otherLease->release(true);
    QTRY_COMPARE(stopped.count(), 1);
    QVERIFY(lmStudio.loaded(QStringLiteral("qwen")).isEmpty());
    QTRY_VERIFY(!FakeLmStudio::isRunning(service));
    QVERIFY(!QFile::exists(runtime.statePath()));
    delete lease;
    delete otherLease;
}

void WhaleAiLocalRuntimeTest::cleansUpAfterCrash()
{
    // An Orcinus that crashed left the service and a loaded model; one that is alive left a model it uses.
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    lmStudio.addModel(QStringLiteral("gemma"), QStringLiteral("Gemma"));
    WhaleAiLocalRuntime starter(lmStudio.options());
    WhaleAiLocalLease *lease = starter.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QTRY_COMPARE(lease->state(), WhaleAiLocalLease::State::Ready);
    const qint64 service = lmStudio.servicePid();
    lmStudio.setLoaded(QStringLiteral("gemma"), {QStringLiteral("gemma")});
    QJsonArray entries = stateEntries(starter);
    entries.append(QJsonObject{{QStringLiteral("pid"), 6666},
                               {QStringLiteral("uses"), QJsonArray{QStringLiteral("gemma")}},
                               {QStringLiteral("instances"),
                                QJsonArray{QJsonObject{{QStringLiteral("model"), QStringLiteral("gemma")}, {QStringLiteral("id"), QStringLiteral("gemma")}}}}});
    lmStudio.addProcess(6666, "orcinus");
    // "Crash": the starter's entry stays (its pid is not in the fake /proc), the starter is not asked.
    FakeLmStudio::writeFile(starter.statePath(), QJsonDocument(QJsonObject{{QStringLiteral("entries"), entries}}).toJson());

    WhaleAiLocalRuntime::Options laterOptions = lmStudio.options();
    laterOptions.pid = 7777;
    WhaleAiLocalRuntime later(laterOptions);
    later.cleanUpAfterCrashes();
    // The crashed one's model and service are gone; the living one's model stays.
    QVERIFY(lmStudio.loaded(QStringLiteral("qwen")).isEmpty());
    QCOMPARE(lmStudio.loaded(QStringLiteral("gemma")), QStringList{QStringLiteral("gemma")});
    // Its service is handed to the living one, which uses LM Studio.
    entries = stateEntries(later);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().toObject().value(QLatin1String("pid")).toInteger(), 6666);
    QCOMPARE(entries.first().toObject().value(QLatin1String("servicePid")).toInteger(), service);
    QVERIFY(FakeLmStudio::isRunning(service));

    // Now that one crashed too: everything goes.
    QFile::remove(lmStudio.procPath() + QStringLiteral("/6666/cmdline"));
    QDir(lmStudio.procPath() + QStringLiteral("/6666")).removeRecursively();
    later.cleanUpAfterCrashes();
    QVERIFY(lmStudio.loaded(QStringLiteral("gemma")).isEmpty());
    QTRY_VERIFY(!FakeLmStudio::isRunning(service));
    QVERIFY(!lmStudio.serverUp());
    QVERIFY(!QFile::exists(later.statePath()));

    // The "crashed" runtime finds nothing of its own any more when it ends.
    delete lease;
}

void WhaleAiLocalRuntimeTest::shutdownWhileLoading()
{
    FakeLmStudio lmStudio;
    lmStudio.addModel(QStringLiteral("qwen"), QStringLiteral("Qwen 27B"));
    lmStudio.setLoadDelay(1500);
    WhaleAiLocalRuntime runtime(lmStudio.options());
    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QSignalSpy failed(lease, &WhaleAiLocalLease::failed);
    QTRY_VERIFY(!matching(lmStudio.requests(), QStringLiteral("POST /api/v1/models/load")).isEmpty() || lmStudio.serverUp());
    QTRY_VERIFY_WITH_TIMEOUT(lease->state() == WhaleAiLocalLease::State::Preparing && lmStudio.serverUp(), 10000);
    QTest::qWait(300);
    const qint64 service = lmStudio.servicePid();
    runtime.shutdown();
    QCOMPARE(failed.count(), 1);
    QTRY_VERIFY(!FakeLmStudio::isRunning(service));
    QVERIFY(!lmStudio.serverUp());
    QVERIFY(!QFile::exists(runtime.statePath()));
    delete lease;
}

void WhaleAiLocalRuntimeTest::otherProvider()
{
    FakeLmStudio lmStudio;
    WhaleAiLocalRuntime runtime(lmStudio.options());
    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("ollama"), QStringLiteral("qwen"), 0, this);
    QCOMPARE(lease->state(), WhaleAiLocalLease::State::Preparing);
    QSignalSpy failed(lease, &WhaleAiLocalLease::failed);
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(lease->errorString().contains(QLatin1String("ollama")));
    QVERIFY(lmStudio.lmsCalls().isEmpty());
    delete lease;
}

QTEST_GUILESS_MAIN(WhaleAiLocalRuntimeTest)

#include "whaleailocalruntimetest.moc"
