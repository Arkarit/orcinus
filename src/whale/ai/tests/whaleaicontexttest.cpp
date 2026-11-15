/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicontext.h"

#include <QDir>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

/**
 * What the AI panel sends along with a message (AI4): the text for the agent, the links, the
 * line for the transcript.
 */
class WhaleAiContextTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyContext();
    void fullContext();
    void subfolders();
    void capsItems();
    void summary();
};

void WhaleAiContextTest::emptyContext()
{
    const WhaleAiContext context;
    QVERIFY(context.isEmpty());
    QVERIFY(WhaleAiContextPrompt::text(context).isEmpty());
    QVERIFY(WhaleAiContextPrompt::summary(context).isEmpty());
    // Only the user's text.
    const QJsonArray blocks = WhaleAiContextPrompt::blocks(context, QStringLiteral("hi"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks.at(0).toObject().value(QLatin1String("text")).toString(), QStringLiteral("hi"));
}

void WhaleAiContextTest::fullContext()
{
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("a/out")));
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("b")));
    const QString folder = dir.filePath(QStringLiteral("a"));
    const WhaleAiContext context{folder,
                                 {folder + QStringLiteral("/x.jpg"), folder + QStringLiteral("/out")},
                                 dir.filePath(QStringLiteral("b")),
                                 {QStringLiteral("/tmp/added.txt")}};

    const QString text = WhaleAiContextPrompt::text(context);
    QVERIFY2(text.startsWith(QLatin1String("Context from the file manager Orcinus")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("- Current folder: %1 (subfolders: out)").arg(folder)), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("- Selected (2): %1/x.jpg, %1/out").arg(folder)));
    QVERIFY(text.contains(QStringLiteral("- Folder in the other split view: %1").arg(dir.filePath(QStringLiteral("b")))));
    QVERIFY(text.contains(QLatin1String("- Added by the user for this message (1): /tmp/added.txt")));

    // Context text, the two folders, two selected, one added, the user's text.
    const QJsonArray blocks = WhaleAiContextPrompt::blocks(context, QStringLiteral("crop them"));
    QCOMPARE(blocks.size(), 7);
    QCOMPARE(blocks.at(0).toObject().value(QLatin1String("text")).toString(), text);
    const QJsonObject folderLink = blocks.at(1).toObject();
    QCOMPARE(folderLink.value(QLatin1String("type")).toString(), QStringLiteral("resource_link"));
    QCOMPARE(folderLink.value(QLatin1String("uri")).toString(), QUrl::fromLocalFile(folder).toString());
    QCOMPARE(folderLink.value(QLatin1String("name")).toString(), QStringLiteral("a"));
    QCOMPARE(folderLink.value(QLatin1String("mimeType")).toString(), QStringLiteral("inode/directory"));
    QCOMPARE(blocks.at(3).toObject().value(QLatin1String("name")).toString(), QStringLiteral("x.jpg"));
    QVERIFY(!blocks.at(3).toObject().contains(QLatin1String("mimeType")));
    QCOMPARE(blocks.at(4).toObject().value(QLatin1String("mimeType")).toString(), QStringLiteral("inode/directory"));
    QCOMPARE(blocks.last().toObject().value(QLatin1String("text")).toString(), QStringLiteral("crop them"));
}

void WhaleAiContextTest::subfolders()
{
    QTemporaryDir dir;
    // An empty folder has none.
    QVERIFY(WhaleAiContextPrompt::text({dir.path(), {}, {}, {}}).contains(QLatin1String("(subfolders: none)")));
    // The first ones by name, then a count.
    for (int i = 0; i < WhaleAiContextPrompt::MaxSubfolders + 5; ++i) {
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("d%1").arg(i, 3, 10, QLatin1Char('0'))));
    }
    const QString text = WhaleAiContextPrompt::text({dir.path(), {}, {}, {}});
    QVERIFY2(text.contains(QLatin1String("(subfolders: d000, d001, ")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral(", d029 … and 5 more)")));
    QVERIFY(!text.contains(QLatin1String("d030")));
}

void WhaleAiContextTest::capsItems()
{
    WhaleAiContext context;
    for (int i = 0; i < WhaleAiContextPrompt::MaxItems + 7; ++i) {
        context.selection.append(QStringLiteral("/f/%1.jpg").arg(i));
    }
    const QString text = WhaleAiContextPrompt::text(context);
    QVERIFY(text.contains(QLatin1String("- Selected (57): /f/0.jpg, ")));
    QVERIFY(text.contains(QStringLiteral("/f/49.jpg … and 7 more, not listed")));
    QVERIFY(!text.contains(QLatin1String("/f/50.jpg")));
    // Context text, 50 links, the user's text.
    QCOMPARE(WhaleAiContextPrompt::blocks(context, QStringLiteral("x")).size(), 52);
}

void WhaleAiContextTest::summary()
{
    QCOMPARE(WhaleAiContextPrompt::summary({QStringLiteral("/a/b"), {QStringLiteral("/a/b/c.txt")}, QStringLiteral("/d"), {}}),
             QStringLiteral("With: folder “b”, selected “c.txt”, other view “d”"));
    QCOMPARE(WhaleAiContextPrompt::summary({{}, {QStringLiteral("/1"), QStringLiteral("/2")}, {}, {QStringLiteral("/x"), QStringLiteral("/y")}}),
             QStringLiteral("With: 2 selected items, 2 added items"));
    QCOMPARE(WhaleAiContextPrompt::summary({{}, {}, {}, {QStringLiteral("/x/notes.md")}}), QStringLiteral("With: “notes.md”"));
}

QTEST_GUILESS_MAIN(WhaleAiContextTest)

#include "whaleaicontexttest.moc"
