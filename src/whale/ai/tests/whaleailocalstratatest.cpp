/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaifakestrata.h"
#include "whaleailocalruntime.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

bool loaded(const FakeStrata &strata, const QString &model)
{
    const std::optional<QByteArray> body = WhaleAiLocalModels::httpGet(QStringLiteral("127.0.0.1"), strata.port(), QStringLiteral("/health"), 1000);
    const QJsonObject health = body ? QJsonDocument::fromJson(*body).object() : QJsonObject();
    return health.value(QLatin1String("loaded")).toBool() && health.value(QLatin1String("model")).toString() == model;
}
}

/**
 * Strata as a local provider of WhaleAiLocalRuntime (AI13c), against FakeStrata.
 */
class WhaleAiLocalStrataTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsAndStops();
    void usesTheUsersStrata();
    void loadsTheUsersUnloadedStrata();
    void endsWhileLoading();
    void oneModelAtATime();
    void handsOver();
    void cleansUpAfterCrash();
};

void WhaleAiLocalStrataTest::startsAndStops()
{
    FakeStrata strata;
    strata.setModel(QStringLiteral("small"), 3);
    WhaleAiLocalRuntime runtime(strata.options());
    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QSignalSpy progress(lease, &WhaleAiLocalLease::progress);
    QSignalSpy ready(lease, &WhaleAiLocalLease::ready);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
    QCOMPARE(lease->instanceId(), QStringLiteral("small"));
    QCOMPARE(lease->model(), QStringLiteral("small"));
    // The phases of its output, with the time, and the warning.
    QStringList texts;
    for (const QList<QVariant> &signal : std::as_const(progress)) {
        texts.append(signal.first().toString());
    }
    QVERIFY2(texts.first().startsWith(QStringLiteral("Starting Strata Small … 0 s")), qPrintable(texts.first()));
    QVERIFY(texts.first().endsWith(QLatin1String("The computer may be slow meanwhile.")));
    const QString all = texts.join(QLatin1Char('\n'));
    QVERIFY2(all.contains(QLatin1String(": loading the model into memory.")) || all.contains(QLatin1String(": filling the graphics card's memory.")),
             qPrintable(all));
    QCOMPARE(strata.servers().size(), 1);
    const qint64 server = strata.servers().first();
    const QJsonObject mine = stateEntries(runtime).first().toObject().value(QLatin1String("strata")).toObject();
    QCOMPARE(mine.value(QLatin1String("pid")).toInteger(), server);
    QCOMPARE(mine.value(QLatin1String("model")).toString(), QStringLiteral("small"));

    QSignalSpy freed(&runtime, &WhaleAiLocalRuntime::modelFreed);
    lease->release(true);
    QTRY_COMPARE(freed.count(), 1);
    QCOMPARE(matching(strata.requests(), QStringLiteral("POST /unload")), QStringList{QStringLiteral("POST /unload?wait=1 application/json")});
    QTRY_VERIFY(strata.servers().isEmpty());
    QVERIFY(!QFile::exists(runtime.statePath()));
    delete lease;
}

void WhaleAiLocalStrataTest::usesTheUsersStrata()
{
    FakeStrata strata;
    strata.startByUser(QStringLiteral("small"));
    QTRY_VERIFY_WITH_TIMEOUT(loaded(strata, QStringLiteral("small")), 10000);
    WhaleAiLocalRuntime runtime(strata.options());

    // The model it serves: used as it is, never unloaded or stopped.
    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QTRY_COMPARE(lease->state(), WhaleAiLocalLease::State::Ready);
    QCOMPARE(strata.servers().size(), 1);
    delete lease;
    QTest::qWait(500);
    QVERIFY(matching(strata.requests(), QStringLiteral("POST")).isEmpty());
    QCOMPARE(strata.servers().size(), 1);

    // Another model: refused, the user's Strata stays.
    lease = runtime.acquire(QStringLiteral("strata"), QStringLiteral("big"), 0, this);
    QSignalSpy failed(lease, &WhaleAiLocalLease::failed);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(lease->errorString(), QStringLiteral("Strata runs Strata Small at the moment, not Strata Big. Orcinus does not stop a Strata it did not start."));
    QVERIFY(loaded(strata, QStringLiteral("small")));
    delete lease;
}

void WhaleAiLocalStrataTest::loadsTheUsersUnloadedStrata()
{
    // The user's Strata unloaded its model (idle): Orcinus loads it, and unloads it again after.
    FakeStrata strata;
    strata.startByUser(QStringLiteral("small"));
    QTRY_VERIFY_WITH_TIMEOUT(loaded(strata, QStringLiteral("small")), 10000);
    WhaleAiLocalModels::httpRequest("POST", QStringLiteral("127.0.0.1"), strata.port(), QStringLiteral("/unload"), "{}", 2000);
    QVERIFY(!loaded(strata, QStringLiteral("small")));
    WhaleAiLocalRuntime runtime(strata.options());
    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QSignalSpy progress(lease, &WhaleAiLocalLease::progress);
    QTRY_COMPARE_WITH_TIMEOUT(lease->state(), WhaleAiLocalLease::State::Ready, 10000);
    QVERIFY(progress.first().first().toString().startsWith(QStringLiteral("Strata loads Strata Small again")));
    QCOMPARE(matching(strata.requests(), QStringLiteral("POST /load")), QStringList{QStringLiteral("POST /load application/json")});
    lease->release(true);
    QTRY_VERIFY(!loaded(strata, QStringLiteral("small")));
    // Its server stays: it is the user's.
    QCOMPARE(strata.servers().size(), 1);
    delete lease;
}

void WhaleAiLocalStrataTest::endsWhileLoading()
{
    FakeStrata strata;
    strata.setModel(QStringLiteral("small"), 0.6, true);
    WhaleAiLocalRuntime runtime(strata.options());
    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QSignalSpy failed(lease, &WhaleAiLocalLease::failed);
    QElapsedTimer timer;
    timer.start();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
    // Seen as gone at once, not after the 10 s a fresh process gets to show.
    QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY2(lease->errorString().startsWith(QLatin1String("Strata ended while it loaded the model:\n")), qPrintable(lease->errorString()));
    QVERIFY(lease->errorString().contains(QLatin1String("exit code 9")));
    QVERIFY(strata.servers().isEmpty());
    QVERIFY(stateEntries(runtime).isEmpty() || stateEntries(runtime).first().toObject().value(QLatin1String("strata")).isUndefined());

    // Not installed, unknown model.
    lease->release(true);
    WhaleAiLocalLease *unknown = runtime.acquire(QStringLiteral("strata"), QStringLiteral("nope"), 0, this);
    QTRY_COMPARE(unknown->errorString(), QStringLiteral("Strata has no model “nope”."));
    WhaleAiLocalRuntime::Options options = strata.options();
    options.discovery.homePath = strata.path() + QStringLiteral("/empty");
    WhaleAiLocalRuntime nowhere(options);
    WhaleAiLocalLease *missing = nowhere.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QTRY_COMPARE(missing->errorString(), QStringLiteral("Strata was not found."));
    delete lease;
    delete unknown;
    delete missing;
}

void WhaleAiLocalStrataTest::oneModelAtATime()
{
    FakeStrata strata;
    WhaleAiLocalRuntime runtime(strata.options(60000));
    WhaleAiLocalLease *small = runtime.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QTRY_COMPARE_WITH_TIMEOUT(small->state(), WhaleAiLocalLease::State::Ready, 10000);
    const qint64 first = strata.servers().first();

    // Held: another model (of Strata, or of LM Studio) is refused.
    WhaleAiLocalLease *big = runtime.acquire(QStringLiteral("strata"), QStringLiteral("big"), 0, this);
    QTRY_COMPARE(big->state(), WhaleAiLocalLease::State::Failed);
    QVERIFY(big->errorString().startsWith(QLatin1String("Strata Small is still in use in another conversation.")));
    WhaleAiLocalLease *other = runtime.acquire(QStringLiteral("lmstudio"), QStringLiteral("qwen"), 0, this);
    QTRY_COMPARE(other->state(), WhaleAiLocalLease::State::Failed);
    QVERIFY(other->errorString().contains(QLatin1String("Only one local model")));
    delete big;
    delete other;

    // Given back (grace period): the next model frees it first, its server goes.
    small->release();
    QCOMPARE(strata.servers(), QList<qint64>{first});
    big = runtime.acquire(QStringLiteral("strata"), QStringLiteral("big"), 0, this);
    QTRY_COMPARE_WITH_TIMEOUT(big->state(), WhaleAiLocalLease::State::Ready, 10000);
    QCOMPARE(strata.servers().size(), 1);
    QVERIFY(strata.servers().first() != first);
    QVERIFY(loaded(strata, QStringLiteral("big")));
    delete small;
    delete big;
    runtime.shutdown();
    QTRY_VERIFY(strata.servers().isEmpty());
}

void WhaleAiLocalStrataTest::handsOver()
{
    FakeStrata strata;
    WhaleAiLocalRuntime runtime(strata.options());
    WhaleAiLocalLease *lease = runtime.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QTRY_COMPARE_WITH_TIMEOUT(lease->state(), WhaleAiLocalLease::State::Ready, 10000);
    WhaleAiLocalRuntime::Options otherOptions = strata.options();
    otherOptions.pid = 5555;
    FakeStrata::writeFile(strata.procPath() + QStringLiteral("/5555/cmdline"), QByteArray("orcinus\0", 8));
    WhaleAiLocalRuntime other(otherOptions);
    WhaleAiLocalLease *otherLease = other.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QTRY_COMPARE(otherLease->state(), WhaleAiLocalLease::State::Ready);

    // The first one is done: the other one now owns the server.
    lease->release(true);
    QTest::qWait(300);
    QVERIFY(loaded(strata, QStringLiteral("small")));
    const QJsonArray entries = stateEntries(runtime);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().toObject().value(QLatin1String("pid")).toInteger(), 5555);
    QVERIFY(!entries.first().toObject().value(QLatin1String("strata")).toObject().isEmpty());

    otherLease->release(true);
    QTRY_VERIFY(strata.servers().isEmpty());
    QVERIFY(!QFile::exists(runtime.statePath()));
    delete lease;
    delete otherLease;
}

void WhaleAiLocalStrataTest::cleansUpAfterCrash()
{
    FakeStrata strata;
    WhaleAiLocalRuntime crashed(strata.options());
    WhaleAiLocalLease *lease = crashed.acquire(QStringLiteral("strata"), QStringLiteral("small"), 0, this);
    QTRY_COMPARE_WITH_TIMEOUT(lease->state(), WhaleAiLocalLease::State::Ready, 10000);
    // Its entry stays; its pid (4000000) is not in the fake /proc, as for a crashed Orcinus.
    WhaleAiLocalRuntime::Options laterOptions = strata.options();
    laterOptions.pid = 7777;
    WhaleAiLocalRuntime later(laterOptions);
    later.cleanUpAfterCrashes();
    QVERIFY(matching(strata.requests(), QStringLiteral("POST /unload")).size() == 1);
    QTRY_VERIFY(strata.servers().isEmpty());
    QVERIFY(!QFile::exists(later.statePath()));
    delete lease;
}

QTEST_GUILESS_MAIN(WhaleAiLocalStrataTest)

#include "whaleailocalstratatest.moc"
