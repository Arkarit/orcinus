/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "views/versioncontrol/versioncontrolobserver.h"
#include "kitemviews/kfileitemmodel.h"
#include "testdir.h"
#include "views/versioncontrol/kversioncontrolplugin.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace
{
/** A plugin that, like the plugins without an own localRepositoryRoot(), only knows its file name. */
class FakePlugin : public KVersionControlPlugin
{
public:
    using KVersionControlPlugin::KVersionControlPlugin;

    QString fileName() const override
    {
        return QStringLiteral(".fakevcs");
    }
    bool beginRetrieval(const QString &) override
    {
        return true;
    }
    void endRetrieval() override
    {
    }
    ItemVersion itemVersion(const KFileItem &) const override
    {
        return NormalVersion;
    }
    QList<QAction *> versionControlActions(const KFileItemList &) const override
    {
        return {};
    }
    QList<QAction *> outOfVersionControlActions(const KFileItemList &) const override
    {
        return {};
    }
};
}

class VersionControlObserverTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testFolderBelowTheRepositoryIsVersioned();
    void testSiblingWithTheRootAsPrefixIsNotVersioned();

private:
    /** Loads \a path (relative to the test folder) and lets the observer verify it. */
    void open(const QString &path);
    bool isVersionControlled() const;
    QString localRepoRoot() const;

    TestDir *m_testDir = nullptr;
    KFileItemModel *m_model = nullptr;
    VersionControlObserver *m_observer = nullptr;
};

void VersionControlObserverTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void VersionControlObserverTest::init()
{
    m_testDir = new TestDir();
    m_testDir->createDir(QStringLiteral("repo/.fakevcs"));
    m_testDir->createDir(QStringLiteral("repo/sub"));
    m_testDir->createFile(QStringLiteral("repo/sub/a.txt"));
    m_testDir->createFile(QStringLiteral("repo/b.txt"));
    m_testDir->createFile(QStringLiteral("repo2/c.txt"));

    m_model = new KFileItemModel(this);
    m_observer = new VersionControlObserver(this);
    // Use only the fake plugin instead of the installed ones.
    m_observer->m_plugins = {new FakePlugin(m_observer)};
    m_observer->m_pluginsInitialized = true;
    m_observer->setModel(m_model);
}

void VersionControlObserverTest::cleanup()
{
    delete m_observer;
    m_observer = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_testDir;
    m_testDir = nullptr;
}

void VersionControlObserverTest::open(const QString &path)
{
    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    m_model->loadDirectory(QUrl::fromLocalFile(m_testDir->path() + QLatin1Char('/') + path));
    QVERIFY(loaded.wait());
    m_observer->verifyDirectory();
}

bool VersionControlObserverTest::isVersionControlled() const
{
    return m_observer->isVersionControlled();
}

QString VersionControlObserverTest::localRepoRoot() const
{
    return m_observer->m_localRepoRoot;
}

void VersionControlObserverTest::testFolderBelowTheRepositoryIsVersioned()
{
    open(QStringLiteral("repo"));
    QVERIFY(isVersionControlled());
    QCOMPARE(localRepoRoot(), m_testDir->path() + QStringLiteral("/repo"));

    open(QStringLiteral("repo/sub"));
    QVERIFY(isVersionControlled());
    QCOMPARE(localRepoRoot(), m_testDir->path() + QStringLiteral("/repo"));
}

void VersionControlObserverTest::testSiblingWithTheRootAsPrefixIsNotVersioned()
{
    open(QStringLiteral("repo"));
    QVERIFY(isVersionControlled());

    // "repo2" begins with the path of "repo" but does not lie inside it (issue #17).
    open(QStringLiteral("repo2"));
    QVERIFY(!isVersionControlled());
}

QTEST_MAIN(VersionControlObserverTest)

#include "versioncontrolobservertest.moc"
