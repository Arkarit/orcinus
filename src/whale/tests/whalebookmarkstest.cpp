/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinbookmarkhandler.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>

/**
 * The bookmarks menu uses Orcinus' own bookmarks file and never writes into Dolphin's data
 * (release plan RL6: Orcinus created ~/.local/share/dolphin on every start).
 */
class WhaleBookmarksTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testOwnFileWithoutDolphin();
    void testImportsDolphinBookmarksOnce();
    void testKeepsExistingOwnFile();

private:
    static QByteArray readFile(const QString &path);
    static void writeFile(const QString &path, const QByteArray &content);

    QString m_dolphinDir;
    QString m_ownDir;
};

void WhaleBookmarksTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    // An application name of its own keeps the test's data directory apart from other tests.
    QCoreApplication::setApplicationName(QStringLiteral("whalebookmarkstest"));
    const QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (QFile::exists(dataLocation + QLatin1String("/kfile/bookmarks.xml"))) {
        QSKIP("KDE's shared kfile/bookmarks.xml exists in the test data directory");
    }
    m_dolphinDir = dataLocation + QLatin1String("/dolphin");
    m_ownDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (QFileInfo::exists(m_dolphinDir)) {
        QSKIP("the test data directory already holds Dolphin data, which this test must not touch");
    }
}

void WhaleBookmarksTest::init()
{
    QDir(m_ownDir).removeRecursively();
}

void WhaleBookmarksTest::cleanup()
{
    QDir(m_ownDir).removeRecursively();
    QDir(m_dolphinDir).removeRecursively();
}

void WhaleBookmarksTest::testOwnFileWithoutDolphin()
{
    const QString file = DolphinBookmarkHandler::bookmarksFile();
    QCOMPARE(file, m_ownDir + QLatin1String("/bookmarks.xml"));
    QVERIFY(QFileInfo(m_ownDir).isDir());
    QVERIFY(!QFileInfo::exists(m_dolphinDir));
}

void WhaleBookmarksTest::testImportsDolphinBookmarksOnce()
{
    const QByteArray dolphinBookmarks("<xbel><bookmark href=\"file:///tmp\"/></xbel>\n");
    writeFile(m_dolphinDir + QLatin1String("/bookmarks.xml"), dolphinBookmarks);

    const QString file = DolphinBookmarkHandler::bookmarksFile();
    QCOMPARE(file, m_ownDir + QLatin1String("/bookmarks.xml"));
    QCOMPARE(readFile(file), dolphinBookmarks);

    // Later changes in Dolphin are not imported again, and Dolphin's file stays as it was.
    writeFile(m_dolphinDir + QLatin1String("/bookmarks.xml"), QByteArray("<xbel/>\n"));
    QCOMPARE(readFile(DolphinBookmarkHandler::bookmarksFile()), dolphinBookmarks);
    QCOMPARE(QDir(m_dolphinDir).entryList(QDir::Files), QStringList{QStringLiteral("bookmarks.xml")});
}

void WhaleBookmarksTest::testKeepsExistingOwnFile()
{
    const QByteArray ownBookmarks("<xbel><bookmark href=\"file:///home\"/></xbel>\n");
    writeFile(m_ownDir + QLatin1String("/bookmarks.xml"), ownBookmarks);
    writeFile(m_dolphinDir + QLatin1String("/bookmarks.xml"), QByteArray("<xbel/>\n"));

    QCOMPARE(readFile(DolphinBookmarkHandler::bookmarksFile()), ownBookmarks);
}

QByteArray WhaleBookmarksTest::readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void WhaleBookmarksTest::writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

QTEST_MAIN(WhaleBookmarksTest)

#include "whalebookmarkstest.moc"
