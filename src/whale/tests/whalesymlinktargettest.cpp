/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphinviewcontainer.h"
#include "kitemviews/kfileitemmodel.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"

#include <KLocalizedString>
#include <KUrlNavigator>

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

/**
 * "Always follow symbolic links to their target": a location that passes through symbolic
 * links is shown at its real path, and the history of the view never holds the link's path.
 */
class WhaleSymlinkTargetTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSetUrl();
    void testTypedPath();
    void testPathBelowLink();
    void testChain();
    void testBackAndForward();
    void testNewTab();
    void testBrokenLink();
    void testDisabled();
    void testOlderEntryStays();

private:
    DolphinViewContainer *showMainWindow();
    QUrl url(const QString &relative) const;
    QList<QUrl> history() const;
    /** Waits until the active view shows @p expected and nothing changes afterwards. */
    bool settlesAt(const QUrl &expected) const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    QString m_basePath;
};

void WhaleSymlinkTargetTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain({"whale"});
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleSymlinkTargetTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("real/target/a.txt"));
    m_dir->createFile(QStringLiteral("real/target/inner/b.txt"));
    m_dir->createFile(QStringLiteral("other/c.txt"));
    // The test directory itself may lie below a symbolic link (e.g. /tmp).
    m_basePath = QFileInfo(m_dir->path()).canonicalFilePath();
    QVERIFY(QFile::link(m_dir->path() + QStringLiteral("/real/target"), m_dir->path() + QStringLiteral("/link")));
    QVERIFY(QFile::link(m_dir->path() + QStringLiteral("/link"), m_dir->path() + QStringLiteral("/chain")));
    QVERIFY(QFile::link(m_dir->path() + QStringLiteral("/missing"), m_dir->path() + QStringLiteral("/broken")));
    GeneralSettings::setFollowSymlinksToTarget(true);
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleSymlinkTargetTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
    GeneralSettings::setFollowSymlinksToTarget(false);
}

DolphinViewContainer *WhaleSymlinkTargetTest::showMainWindow()
{
    m_mainWindow->openDirectories({QUrl::fromLocalFile(m_basePath)}, false);
    m_mainWindow->resize(800, 600);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() > 0;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QUrl WhaleSymlinkTargetTest::url(const QString &relative) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + relative);
}

QList<QUrl> WhaleSymlinkTargetTest::history() const
{
    const KUrlNavigator *navigator = m_mainWindow->activeViewContainer()->urlNavigatorInternalWithHistory();
    QList<QUrl> result;
    for (int i = navigator->historySize() - 1; i >= 0; --i) {
        result.append(navigator->locationUrl(i).adjusted(QUrl::StripTrailingSlash));
    }
    return result;
}

bool WhaleSymlinkTargetTest::settlesAt(const QUrl &expected) const
{
    const auto shows = [this, expected]() {
        const DolphinViewContainer *container = m_mainWindow->activeViewContainer();
        return container->url().matches(expected, QUrl::StripTrailingSlash) && container->view()->url().matches(expected, QUrl::StripTrailingSlash);
    };
    if (!QTest::qWaitFor(shows, 5000)) {
        return false;
    }
    QTest::qWait(100);
    return shows();
}

void WhaleSymlinkTargetTest::testSetUrl()
{
    // As the Places panel, bookmarks and plugins navigate.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setUrl(url(QStringLiteral("link")));
    const QUrl target = QUrl::fromLocalFile(m_basePath + QStringLiteral("/real/target"));
    QVERIFY(settlesAt(target));
    QCOMPARE(history(), QList<QUrl>({QUrl::fromLocalFile(m_basePath), target}));
    QTRY_COMPARE(container->view()->itemsCount(), 2);
}

void WhaleSymlinkTargetTest::testTypedPath()
{
    // Typed paths, the breadcrumb menus and pasted paths go straight into the navigator.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->urlNavigatorInternalWithHistory()->setLocationUrl(url(QStringLiteral("link")));
    const QUrl target = QUrl::fromLocalFile(m_basePath + QStringLiteral("/real/target"));
    QVERIFY(settlesAt(target));
    QCOMPARE(history(), QList<QUrl>({QUrl::fromLocalFile(m_basePath), target}));
}

void WhaleSymlinkTargetTest::testPathBelowLink()
{
    // Not only a link itself: every path through a link is resolved, as "cd -P" does.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setUrl(url(QStringLiteral("link/inner")));
    QVERIFY(settlesAt(QUrl::fromLocalFile(m_basePath + QStringLiteral("/real/target/inner"))));
}

void WhaleSymlinkTargetTest::testChain()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setUrl(url(QStringLiteral("chain")));
    const QUrl target = QUrl::fromLocalFile(m_basePath + QStringLiteral("/real/target"));
    QVERIFY(settlesAt(target));
    QCOMPARE(history(), QList<QUrl>({QUrl::fromLocalFile(m_basePath), target}));
}

void WhaleSymlinkTargetTest::testBackAndForward()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    const QUrl base = QUrl::fromLocalFile(m_basePath);
    const QUrl target = QUrl::fromLocalFile(m_basePath + QStringLiteral("/real/target"));
    const QUrl inner = QUrl::fromLocalFile(m_basePath + QStringLiteral("/real/target/inner"));
    container->setUrl(url(QStringLiteral("link")));
    QVERIFY(settlesAt(target));
    container->setUrl(url(QStringLiteral("chain/inner")));
    QVERIFY(settlesAt(inner));
    QCOMPARE(history(), QList<QUrl>({base, target, inner}));

    // Going back never lands on a link that would lead forward again.
    KUrlNavigator *navigator = container->urlNavigatorInternalWithHistory();
    QVERIFY(navigator->goBack());
    QVERIFY(settlesAt(target));
    QVERIFY(navigator->goBack());
    QVERIFY(settlesAt(base));
    QVERIFY(!navigator->goBack());
    QVERIFY(navigator->goForward());
    QVERIFY(settlesAt(target));
    QVERIFY(navigator->goForward());
    QVERIFY(settlesAt(inner));
    QCOMPARE(history(), QList<QUrl>({base, target, inner}));
}

void WhaleSymlinkTargetTest::testNewTab()
{
    QVERIFY(showMainWindow());
    m_mainWindow->openNewTabAndActivate(url(QStringLiteral("link")));
    const QUrl target = QUrl::fromLocalFile(m_basePath + QStringLiteral("/real/target"));
    QVERIFY(settlesAt(target));
    QCOMPARE(history(), QList<QUrl>({target}));
}

void WhaleSymlinkTargetTest::testBrokenLink()
{
    // A broken link has no target: it stays as it is, as without the setting.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setUrl(url(QStringLiteral("broken")));
    QVERIFY(settlesAt(url(QStringLiteral("broken"))));
}

void WhaleSymlinkTargetTest::testDisabled()
{
    GeneralSettings::setFollowSymlinksToTarget(false);
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setUrl(url(QStringLiteral("link")));
    QVERIFY(settlesAt(url(QStringLiteral("link"))));
    QCOMPARE(history().constLast(), url(QStringLiteral("link")));
    QTRY_COMPARE(container->view()->itemsCount(), 2);
}

void WhaleSymlinkTargetTest::testOlderEntryStays()
{
    // An entry from before the setting was enabled is shown as it is when going back to it:
    // replacing it would drop the newer entries.
    GeneralSettings::setFollowSymlinksToTarget(false);
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->setUrl(url(QStringLiteral("link")));
    QVERIFY(settlesAt(url(QStringLiteral("link"))));
    GeneralSettings::setFollowSymlinksToTarget(true);
    const QUrl other = QUrl::fromLocalFile(m_basePath + QStringLiteral("/other"));
    container->setUrl(other);
    QVERIFY(settlesAt(other));

    KUrlNavigator *navigator = container->urlNavigatorInternalWithHistory();
    QVERIFY(navigator->goBack());
    QVERIFY(settlesAt(url(QStringLiteral("link"))));
    QVERIFY(navigator->goForward());
    QVERIFY(settlesAt(other));
    QCOMPARE(history(), QList<QUrl>({QUrl::fromLocalFile(m_basePath), url(QStringLiteral("link")), other}));
}

QTEST_MAIN(WhaleSymlinkTargetTest)

#include "whalesymlinktargettest.moc"
