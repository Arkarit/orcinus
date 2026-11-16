/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaichangelog.h"

#include <QSignalSpy>
#include <QTest>

class WhaleAiChangeLogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRecordAndUndo();
    void testOnePerWindow();
};

void WhaleAiChangeLogTest::testRecordAndUndo()
{
    WhaleAiChangeLog log;
    QSignalSpy changed(&log, &WhaleAiChangeLog::changed);
    QStringList order;
    int value = 1;

    // The state before the first change is kept; later changes of the same thing add nothing.
    log.record(QStringLiteral("setting:A"), QStringLiteral("A"), [&order, &value, old = value] {
        order.append(QStringLiteral("A"));
        value = old;
    });
    value = 2;
    log.record(QStringLiteral("setting:A"), QStringLiteral("A again"), [&order] {
        order.append(QStringLiteral("A again"));
    });
    value = 3;
    log.record(QStringLiteral("view:B"), QStringLiteral("B"), [&order] {
        order.append(QStringLiteral("B"));
    });
    QCOMPARE(log.count(), 2);
    QVERIFY(log.contains(QStringLiteral("setting:A")));
    QCOMPARE(changed.count(), 2);

    // Newest first; the log is empty afterwards.
    QCOMPARE(log.undo(), (QStringList{QStringLiteral("B"), QStringLiteral("A")}));
    QCOMPARE(order, (QStringList{QStringLiteral("B"), QStringLiteral("A")}));
    QCOMPARE(value, 1);
    QCOMPARE(log.count(), 0);
    QCOMPARE(changed.count(), 3);
    QVERIFY(log.undo().isEmpty());
    QCOMPARE(changed.count(), 3);

    // Clearing forgets without undoing.
    log.record(QStringLiteral("setting:A"), QStringLiteral("A"), [&order] {
        order.append(QStringLiteral("cleared"));
    });
    log.clear();
    QCOMPARE(log.count(), 0);
    QVERIFY(!order.contains(QStringLiteral("cleared")));
}

void WhaleAiChangeLogTest::testOnePerWindow()
{
    QObject window;
    WhaleAiChangeLog *log = WhaleAiChangeLog::of(&window);
    QVERIFY(log);
    QCOMPARE(WhaleAiChangeLog::of(&window), log);
    QCOMPARE(log->parent(), &window);
    QCOMPARE(WhaleAiChangeLog::of(nullptr), nullptr);
}

QTEST_GUILESS_MAIN(WhaleAiChangeLogTest)

#include "whaleaichangelogtest.moc"
