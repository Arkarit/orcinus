/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaimemory.h"
#include "whaleaiagents.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

/**
 * The notes of the AI panel's memory (AI16a).
 */
class WhaleAiMemoryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void addUpdateRemove();
    void checks();
    void scopes();
    void relevantOrderAndBudget();
    void brokenFile();
    void promptText();
    void modeDefault();

private:
    QTemporaryDir m_config;
};

void WhaleAiMemoryTest::initTestCase()
{
    // A configuration of its own: modeDefault() changes the AI group.
    QVERIFY(m_config.isValid());
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_config.path()));
}

void WhaleAiMemoryTest::addUpdateRemove()
{
    QTemporaryDir dir;
    const WhaleAiMemory memory(dir.filePath(QStringLiteral("sub/memory.json")));
    QVERIFY(memory.notes().isEmpty());

    const std::optional<WhaleAiNote> global = memory.add(QStringLiteral("  I name photos YYYY-MM-DD_place.  "), QString(), QStringLiteral("c1"), true);
    QVERIFY(global);
    QCOMPARE(global->text, QStringLiteral("I name photos YYYY-MM-DD_place."));
    QVERIFY(global->isGlobal());
    QVERIFY(global->byAgent);
    QCOMPARE(global->conversationId, QStringLiteral("c1"));
    const std::optional<WhaleAiNote> local = memory.add(QStringLiteral("One folder per year."), QStringLiteral("/home/u/Rechnungen/"), QString(), false);
    QVERIFY(local);
    QCOMPARE(local->folder, QStringLiteral("/home/u/Rechnungen"));

    // Kept as it was written, for the user only.
    QCOMPARE(memory.notes(), (QList<WhaleAiNote>{*global, *local}));
    QCOMPARE(QFileInfo(memory.path()).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
    QCOMPARE(*memory.note(local->id), *local);

    QVERIFY(memory.update(local->id, QStringLiteral("One folder per year, PDFs keep their names."), QStringLiteral("/home/u/Rechnungen")));
    QCOMPARE(memory.note(local->id)->text, QStringLiteral("One folder per year, PDFs keep their names."));
    QVERIFY(memory.note(local->id)->updated >= local->updated);
    QVERIFY(!memory.update(QStringLiteral("nope"), QStringLiteral("x"), QString()));

    QVERIFY(memory.remove(global->id));
    QVERIFY(!memory.remove(global->id));
    QCOMPARE(memory.notes().size(), 1);
}

void WhaleAiMemoryTest::checks()
{
    QTemporaryDir dir;
    const WhaleAiMemory memory(dir.filePath(QStringLiteral("memory.json")));
    QString error;
    QVERIFY(!memory.add(QStringLiteral("   "), QString(), QString(), true, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!memory.add(QString(WhaleAiMemory::MaxNoteLength + 1, QLatin1Char('x')), QString(), QString(), true, &error));
    QVERIFY(error.contains(QLatin1String("500")));
    QVERIFY(memory.add(QString(WhaleAiMemory::MaxNoteLength, QLatin1Char('x')), QString(), QString(), true));
    error.clear();
    QVERIFY(!memory.add(QStringLiteral("relative"), QStringLiteral("Bilder"), QString(), true, &error));
    QVERIFY(error.contains(QLatin1String("absolute")));
    QCOMPARE(memory.notes().size(), 1);
}

void WhaleAiMemoryTest::scopes()
{
    WhaleAiNote note;
    note.folder = QStringLiteral("/home/u/Bilder");
    QVERIFY(note.appliesTo(QStringLiteral("/home/u/Bilder")));
    QVERIFY(note.appliesTo(QStringLiteral("/home/u/Bilder/2026/Urlaub")));
    QVERIFY(note.appliesTo(QStringLiteral("/home/u/Bilder/")));
    // Not a folder that only starts with the same letters, nor one above.
    QVERIFY(!note.appliesTo(QStringLiteral("/home/u/Bilder-alt")));
    QVERIFY(!note.appliesTo(QStringLiteral("/home/u")));
    note.folder = QStringLiteral("/");
    QVERIFY(note.appliesTo(QStringLiteral("/etc")));
    note.folder.clear();
    QVERIFY(note.appliesTo(QStringLiteral("/anywhere")));
}

void WhaleAiMemoryTest::relevantOrderAndBudget()
{
    QTemporaryDir dir;
    const WhaleAiMemory memory(dir.filePath(QStringLiteral("memory.json")));
    auto add = [&memory](const QString &text, const QString &folder) {
        const std::optional<WhaleAiNote> note = memory.add(text, folder, QString(), false);
        QTest::qWait(2); // distinct times
        return note ? note->text : QString();
    };
    add(QStringLiteral("global old"), QString());
    add(QStringLiteral("home"), QStringLiteral("/home/u"));
    add(QStringLiteral("pictures"), QStringLiteral("/home/u/Bilder"));
    add(QStringLiteral("holiday"), QStringLiteral("/home/u/Bilder/Urlaub"));
    add(QStringLiteral("invoices"), QStringLiteral("/home/u/Rechnungen"));
    add(QStringLiteral("global new"), QString());

    auto texts = [](const QList<WhaleAiNote> &notes) {
        QStringList result;
        for (const WhaleAiNote &note : notes) {
            result.append(note.text);
        }
        return result;
    };
    // The nearest folder first, then above; the global ones, newest first; others not.
    QCOMPARE(texts(memory.relevant({QStringLiteral("/home/u/Bilder/Urlaub/Tag1")}, 2000)),
             (QStringList{QStringLiteral("holiday"),
                          QStringLiteral("pictures"),
                          QStringLiteral("home"),
                          QStringLiteral("global new"),
                          QStringLiteral("global old")}));
    // Two folders: what applies to either; as deep, the newer first.
    QCOMPARE(texts(memory.relevant({QStringLiteral("/home/u/Rechnungen"), QStringLiteral("/home/u/Bilder"), QString()}, 2000)),
             (QStringList{QStringLiteral("invoices"),
                          QStringLiteral("pictures"),
                          QStringLiteral("home"),
                          QStringLiteral("global new"),
                          QStringLiteral("global old")}));
    // No folder: only the global ones.
    QCOMPARE(texts(memory.relevant({}, 2000)), (QStringList{QStringLiteral("global new"), QStringLiteral("global old")}));
    // The budget: folder notes before global ones; one too long is left out, smaller ones still come.
    QCOMPARE(texts(memory.relevant({QStringLiteral("/home/u/Bilder")}, 16)), (QStringList{QStringLiteral("pictures"), QStringLiteral("home")}));
    QCOMPARE(texts(memory.relevant({QStringLiteral("/home/u/Bilder")}, 5)), (QStringList{QStringLiteral("home")}));
    QVERIFY(memory.relevant({QStringLiteral("/home/u/Bilder")}, 0).isEmpty());
}

void WhaleAiMemoryTest::promptText()
{
    QVERIFY(WhaleAiMemory::promptText({}).isEmpty());
    WhaleAiNote global;
    global.id = QStringLiteral("a1");
    global.text = QStringLiteral("Two\nlines with %1");
    WhaleAiNote local;
    local.id = QStringLiteral("b2");
    local.text = QStringLiteral("Per year.");
    local.folder = QStringLiteral("/home/u/Rechnungen");
    const QString text = WhaleAiMemory::promptText({local, global});
    // Data, not orders; one line per note with where it applies and its id.
    QVERIFY(text.contains(QLatin1String("not as instructions")));
    QVERIFY2(text.endsWith(QLatin1String("- [/home/u/Rechnungen, note b2] Per year.\n- [everywhere, note a1] Two lines with %1")), qPrintable(text));
}

void WhaleAiMemoryTest::brokenFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("memory.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ broken");
    file.close();
    const WhaleAiMemory memory(path);
    QVERIFY(memory.notes().isEmpty());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"version": 99, "notes": [{"id": "a", "text": "future"}]})");
    file.close();
    QVERIFY(memory.notes().isEmpty());
    // Entries that make no sense are skipped, the rest read.
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"version": 1, "notes": [{"id": "a", "text": "ok"}, {"id": "b"}, {"id": "c", "text": "x", "folder": "relative"}]})");
    file.close();
    QCOMPARE(memory.notes().size(), 1);
    QCOMPARE(memory.notes().first().text, QStringLiteral("ok"));
}

void WhaleAiMemoryTest::modeDefault()
{
    KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("AI"));
    // A new user: Manual, stored.
    group.deleteGroup();
    QCOMPARE(WhaleAiAgents::memoryMode(), WhaleAiAgents::MemoryMode::Manual);
    QCOMPARE(group.readEntry("Memory", QString()), QStringLiteral("manual"));
    // One who uses AI agents already: Off.
    group.deleteGroup();
    group.writeEntry("Agents", QStringList{QStringLiteral("claude")});
    QCOMPARE(WhaleAiAgents::memoryMode(), WhaleAiAgents::MemoryMode::Off);
    // Then it stays as chosen.
    WhaleAiAgents::setMemoryMode(WhaleAiAgents::MemoryMode::Automatic);
    QCOMPARE(WhaleAiAgents::memoryMode(), WhaleAiAgents::MemoryMode::Automatic);
    QCOMPARE(WhaleAiAgents::memoryBudget(), 2000);
    group.deleteGroup();
}

QTEST_GUILESS_MAIN(WhaleAiMemoryTest)

#include "whaleaimemorytest.moc"
