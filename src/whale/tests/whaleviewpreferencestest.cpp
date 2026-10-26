/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewpreferences.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"
#include "whaleviewstate.h"

#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTest>

namespace
{
const QString treemapId = QStringLiteral("whale_treemap");
const QString testListId = QStringLiteral("whale_testlist");
const QString fileListId = WhaleViewHost::dolphinViewId();

/// Warnings of Whale's view code; a fallback must not flood the log with them.
QRegularExpression whaleWarnings()
{
    return QRegularExpression(QStringLiteral("view plugin|Cannot restore the view|preferred view|org\\.kde\\.whale"),
                              QRegularExpression::CaseInsensitiveOption);
}

WhaleViewPreferences *preferences()
{
    return WhaleViewPreferences::instance();
}
}

/**
 * O3: the preferred view of a location. The key semantics and the file on their own, then
 * the behavior in real main windows: navigation, fallbacks, history, session, split view.
 */
class WhaleViewPreferencesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testLocationKey_data();
    void testLocationKey();
    void testPersistence();
    void testOtherProcessWrites();
    void testDamagedEntriesAreIgnored();

    void testNoPreferenceKeepsBehavior();
    void testPreferenceFollowsLocation();
    void testOnlyTheUserChoiceIsRemembered();
    void testUnsupportedLocation();
    void testMissingPlugin();
    void testHistoryHasPriority();
    void testSessionHasPriority();
    void testSplitView();
    void testNewViewArea();
    void testClearPreference();
    void testFileListModes();

private:
    DolphinViewContainer *showMainWindow(const QUrl &url);
    /// Chooses @p viewId in the view switcher, as the user does.
    bool choose(const QString &viewId);
    QAction *switcherEntry(const QString &viewId) const;
    WhaleViewRegistry *registry() const;
    QUrl url(const QString &name) const;
    void navigate(DolphinViewContainer *container, const QUrl &url);
    QString preferencesFile() const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

void WhaleViewPreferencesTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
    // The preferences of this test live in the test's state directory, never in the user's.
    QVERIFY(preferencesFile().startsWith(QDir::homePath()));
    QCOMPARE(QDir::homePath(), m_tmpHome.path());
}

void WhaleViewPreferencesTest::init()
{
    QFile::remove(preferencesFile());
    QCOMPARE(preferences()->count(), 0);
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a/big.bin"), QByteArray(40000, 'a'));
    m_dir->createFile(QStringLiteral("a/sub/small.txt"), QByteArray(3000, 's'));
    m_dir->createFile(QStringLiteral("b/one.txt"), QByteArray(2000, 'b'));
    m_dir->createFile(QStringLiteral("c/two.txt"), QByteArray(1000, 'c'));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleViewPreferencesTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
}

QString WhaleViewPreferencesTest::preferencesFile() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/whaleviewpreferencesrc");
}

DolphinViewContainer *WhaleViewPreferencesTest::showMainWindow(const QUrl &url)
{
    m_mainWindow->openDirectories({url}, false);
    m_mainWindow->resize(1000, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowExposed(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    if (!QTest::qWaitFor(
            [container, url]() {
                return container->url() == url && container->view()->itemsCount() > 0;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

WhaleViewRegistry *WhaleViewPreferencesTest::registry() const
{
    return m_mainWindow->findChild<WhaleViewActions *>()->registry();
}

QAction *WhaleViewPreferencesTest::switcherEntry(const QString &viewId) const
{
    // The view mode entry of the view; for the file list, the entry of its current mode.
    return m_mainWindow->findChild<WhaleViewActions *>()->viewModeAction(viewId);
}

bool WhaleViewPreferencesTest::choose(const QString &viewId)
{
    QAction *entry = switcherEntry(viewId);
    if (!entry || !entry->isEnabled()) {
        return false;
    }
    entry->trigger();
    return m_mainWindow->activeViewContainer()->viewHost()->currentViewId() == viewId;
}

QUrl WhaleViewPreferencesTest::url(const QString &name) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + name);
}

void WhaleViewPreferencesTest::navigate(DolphinViewContainer *container, const QUrl &url)
{
    // A new navigation (a new history entry), like entering the location.
    container->setUrl(url);
    QTRY_COMPARE(container->url(), url);
}

void WhaleViewPreferencesTest::testLocationKey_data()
{
    QTest::addColumn<QUrl>("url");
    QTest::addColumn<QString>("key");

    QTest::newRow("local") << QUrl::fromLocalFile(QStringLiteral("/home/u/Pictures")) << QStringLiteral("file:///home/u/Pictures");
    QTest::newRow("trailing slash") << QUrl::fromLocalFile(QStringLiteral("/home/u/Pictures/")) << QStringLiteral("file:///home/u/Pictures");
    QTest::newRow("trailing slash, URL text") << QUrl(QStringLiteral("file:///home/u/Pictures/")) << QStringLiteral("file:///home/u/Pictures");
    QTest::newRow("double slashes") << QUrl::fromLocalFile(QStringLiteral("/home//u///Pictures//")) << QStringLiteral("file:///home/u/Pictures");
    QTest::newRow("dot segments") << QUrl::fromLocalFile(QStringLiteral("/home/u/./x/../Pictures")) << QStringLiteral("file:///home/u/Pictures");
    QTest::newRow("root") << QUrl::fromLocalFile(QStringLiteral("/")) << QStringLiteral("file:///");
    QTest::newRow("query and fragment of a local file")
        << QUrl(QStringLiteral("file:///home/u/Pictures?x=1#frag")) << QStringLiteral("file:///home/u/Pictures");
    // Symbolic links are not resolved: the key is the path the user went to.
    QTest::newRow("unicode") << QUrl::fromLocalFile(QStringLiteral("/home/u/Bilder/Übersicht ✓ 日本"))
                             << QStringLiteral("file:///home/u/Bilder/%C3%9Cbersicht%20%E2%9C%93%20%E6%97%A5%E6%9C%AC");
    QTest::newRow("unicode, encoded input") << QUrl(QStringLiteral("file:///home/u/Bilder/%C3%9Cbersicht%20%E2%9C%93%20%E6%97%A5%E6%9C%AC/"))
                                            << QStringLiteral("file:///home/u/Bilder/%C3%9Cbersicht%20%E2%9C%93%20%E6%97%A5%E6%9C%AC");
    QTest::newRow("special characters") << QUrl::fromLocalFile(QStringLiteral("/tmp/a[1]=b,c#d?e%f;g"))
                                        << QStringLiteral("file:///tmp/a%5B1%5D=b,c%23d%3Fe%25f;g");
    QTest::newRow("trash") << QUrl(QStringLiteral("trash:/")) << QStringLiteral("trash:/");
    QTest::newRow("trash without path") << QUrl(QStringLiteral("trash:")) << QStringLiteral("trash:/");
    QTest::newRow("remote") << QUrl(QStringLiteral("sftp://user:secret@Example.org:22/srv/data/")) << QStringLiteral("sftp://user@example.org:22/srv/data");
    QTest::newRow("remote dot segments") << QUrl(QStringLiteral("smb://server/share/a/../b")) << QStringLiteral("smb://server/share/b");
    QTest::newRow("query of a search stays") << QUrl(QStringLiteral("baloosearch:/?json=%7B%7D#x")) << QStringLiteral("baloosearch:/?json=%7B%7D");
    QTest::newRow("empty") << QUrl() << QString();
    QTest::newRow("invalid") << QUrl(QStringLiteral("http://[::1")) << QString();
}

void WhaleViewPreferencesTest::testLocationKey()
{
    QFETCH(QUrl, url);
    QFETCH(QString, key);
    QCOMPARE(WhaleViewPreferences::locationKey(url), key);
}

void WhaleViewPreferencesTest::testPersistence()
{
    const QString path = m_tmpHome.filePath(QStringLiteral("persistence-rc"));
    const QUrl pictures = QUrl::fromLocalFile(QStringLiteral("/home/u/Bilder/Übersicht [alt]=1"));
    const QUrl longUrl = QUrl::fromLocalFile(QStringLiteral("/deep") + QStringLiteral("/verylongfoldername").repeated(200));
    const QUrl trash(QStringLiteral("trash:/"));
    {
        WhaleViewPreferences store(KSharedConfig::openConfig(path, KConfig::SimpleConfig));
        store.setPreferredView(QUrl::fromLocalFile(QStringLiteral("/home/u/Pictures/")), treemapId);
        store.setPreferredView(pictures, testListId);
        store.setPreferredView(longUrl, treemapId);
        store.setPreferredView(trash, fileListId);
        QCOMPARE(store.count(), 4);
    }

    // A "restart": a new store reads the file.
    KSharedConfig::openConfig(path, KConfig::SimpleConfig)->reparseConfiguration();
    WhaleViewPreferences store(KSharedConfig::openConfig(path, KConfig::SimpleConfig));
    QCOMPARE(store.count(), 4);
    QCOMPARE(store.preferredView(QUrl::fromLocalFile(QStringLiteral("/home/u/Pictures"))), treemapId);
    QCOMPARE(store.preferredView(pictures), testListId);
    QCOMPARE(store.preferredView(longUrl), treemapId);
    QCOMPARE(store.preferredView(QUrl(QStringLiteral("trash:"))), fileListId);
    // Exactly that location: no inheritance by the folders below it.
    QVERIFY(store.preferredView(QUrl::fromLocalFile(QStringLiteral("/home/u/Pictures/sub"))).isEmpty());
    QVERIFY(store.preferredView(QUrl::fromLocalFile(QStringLiteral("/home/u"))).isEmpty());

    // One plain group, fixed-length keys, nothing of the URLs in the keys.
    KConfig file(path, KConfig::SimpleConfig);
    QCOMPARE(file.groupList(), QStringList{QStringLiteral("PreferredViews")});
    const QStringList keys = file.group(QStringLiteral("PreferredViews")).keyList();
    QCOMPARE(keys.count(), 4);
    for (const QString &key : keys) {
        QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(key).hasMatch());
    }

    // Clearing writes at once, too.
    QVERIFY(store.clearPreferredView(pictures));
    QVERIFY(!store.clearPreferredView(pictures));
    KSharedConfig::openConfig(path, KConfig::SimpleConfig)->reparseConfiguration();
    WhaleViewPreferences reread(KSharedConfig::openConfig(path, KConfig::SimpleConfig));
    QCOMPARE(reread.count(), 3);
    QVERIFY(reread.preferredView(pictures).isEmpty());
}

void WhaleViewPreferencesTest::testOtherProcessWrites()
{
    // Another Whale process writes the file; this one notices it at its next lookup.
    const QString path = m_tmpHome.filePath(QStringLiteral("shared-rc"));
    const QUrl a = QUrl::fromLocalFile(QStringLiteral("/a"));
    const QUrl b = QUrl::fromLocalFile(QStringLiteral("/b"));
    WhaleViewPreferences mine(KSharedConfig::openConfig(path, KConfig::SimpleConfig));
    mine.setPreferredView(a, treemapId);
    QVERIFY(mine.preferredView(b).isEmpty());

    // The other process: a KConfig object of its own, writing an entry in the documented
    // format (hash of the location key -> view ID, location key).
    QTest::qWait(10); // a modification time of its own
    {
        KConfig otherProcess(path, KConfig::SimpleConfig);
        const QString locationKey = WhaleViewPreferences::locationKey(b);
        const QString entryKey = QString::fromLatin1(QCryptographicHash::hash(locationKey.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
        otherProcess.group(QStringLiteral("PreferredViews")).writeEntry(entryKey, QStringList{testListId, locationKey});
    }
    QCOMPARE(mine.preferredView(b), testListId);
    QCOMPARE(mine.preferredView(a), treemapId);
    QCOMPARE(mine.count(), 2);

    // Writing keeps what the other process wrote.
    mine.setPreferredView(QUrl::fromLocalFile(QStringLiteral("/c")), fileListId);
    KConfig check(path, KConfig::SimpleConfig);
    QCOMPARE(check.group(QStringLiteral("PreferredViews")).keyList().count(), 3);
}

void WhaleViewPreferencesTest::testDamagedEntriesAreIgnored()
{
    const QString path = m_tmpHome.filePath(QStringLiteral("damaged-rc"));
    {
        WhaleViewPreferences store(KSharedConfig::openConfig(path, KConfig::SimpleConfig));
        store.setPreferredView(QUrl::fromLocalFile(QStringLiteral("/ok")), treemapId);
    }
    {
        KConfig file(path, KConfig::SimpleConfig);
        KConfigGroup group = file.group(QStringLiteral("PreferredViews"));
        group.writeEntry(QStringLiteral("0123456789abcdef0123456789abcdef"), QStringList{treemapId, QStringLiteral("file:///wrong-hash")});
        group.writeEntry(QStringLiteral("garbage"), QStringLiteral("no list"));
        group.writeEntry(QStringLiteral("empty"), QStringList{QString(), QStringLiteral("file:///x")});
    }
    KSharedConfig::openConfig(path, KConfig::SimpleConfig)->reparseConfiguration();
    QTest::failOnWarning(whaleWarnings());
    WhaleViewPreferences store(KSharedConfig::openConfig(path, KConfig::SimpleConfig));
    QCOMPARE(store.count(), 1);
    QCOMPARE(store.preferredView(QUrl::fromLocalFile(QStringLiteral("/ok"))), treemapId);
    QVERIFY(store.preferredView(QUrl(QStringLiteral("file:///wrong-hash"))).isEmpty());
}

void WhaleViewPreferencesTest::testNoPreferenceKeepsBehavior()
{
    // Nothing remembered: the file list, as always.
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(host->isDolphinViewShown());

    // Without a preference a new location keeps the shown view (behavior before O3).
    QVERIFY(host->showPluginView(testListId, registry()->plugin(testListId)));
    navigate(container, url(QStringLiteral("b")));
    QCOMPARE(host->currentViewId(), testListId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("kept"));
    // Nothing of this was remembered: none of it was the user's choice in the switcher.
    QCOMPARE(preferences()->count(), 0);
}

void WhaleViewPreferencesTest::testPreferenceFollowsLocation()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();

    // A: the treemap, chosen by the user.
    QVERIFY(choose(treemapId));
    QCOMPARE(preferences()->preferredView(url(QStringLiteral("a"))), treemapId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("user"));

    // B: no preference yet; the user chooses the file list there.
    navigate(container, url(QStringLiteral("b")));
    QCOMPARE(host->currentViewId(), treemapId);
    QVERIFY(choose(fileListId));
    QCOMPARE(preferences()->preferredView(url(QStringLiteral("b"))), fileListId);

    // New navigation (not the history) to A and B, several times.
    for (int i = 0; i < 3; ++i) {
        navigate(container, url(QStringLiteral("a/")));
        QCOMPARE(host->currentViewId(), treemapId);
        QCOMPARE(host->viewOrigin(), QStringLiteral("preference"));
        QCOMPARE(host->lastRestore().source, QStringLiteral("preference"));
        QVERIFY(!host->lastRestore().fallback);
        navigate(container, url(QStringLiteral("b")));
        QVERIFY(host->isDolphinViewShown());
        QCOMPARE(host->viewOrigin(), QStringLiteral("preference"));
    }
    // The switcher shows the view that came from the preference.
    navigate(container, url(QStringLiteral("a")));
    QVERIFY(switcherEntry(treemapId)->isChecked());

    // A subfolder of A has no preference of its own (no inheritance): the treemap stays as
    // the shown view, but nothing is remembered for it.
    navigate(container, url(QStringLiteral("a/sub")));
    QCOMPARE(host->currentViewId(), treemapId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("kept"));
    QVERIFY(preferences()->preferredView(url(QStringLiteral("a/sub"))).isEmpty());
    QCOMPARE(preferences()->count(), 2);
}

void WhaleViewPreferencesTest::testOnlyTheUserChoiceIsRemembered()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();

    // Programmatic switches (history, session, tools) are no choice of the user.
    QVERIFY(host->showPluginView(testListId, registry()->plugin(testListId)));
    host->showDolphinView();
    QCOMPARE(preferences()->count(), 0);

    // The file list's modes choose nothing while the file list is shown (testFileListModes).
    QVERIFY(choose(fileListId));
    QCOMPARE(preferences()->count(), 0);
    QVERIFY(choose(testListId));
    QCOMPARE(preferences()->preferredView(url(QStringLiteral("a"))), testListId);
    // Choosing the view that is shown already confirms it as the location's view.
    QVERIFY(choose(testListId));
    QCOMPARE(preferences()->preferredView(url(QStringLiteral("a"))), testListId);
    QCOMPARE(preferences()->count(), 1);
}

void WhaleViewPreferencesTest::testUnsupportedLocation()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(choose(treemapId));

    // The trash: the treemap cannot show it, the file list does (M9) ...
    const QUrl trash(QStringLiteral("trash:/"));
    navigate(container, trash);
    QTRY_VERIFY(host->isDolphinViewShown());
    QCOMPARE(host->lastRestore().source, QStringLiteral("location"));
    QCOMPARE(host->viewOrigin(), QStringLiteral("fallback"));
    QVERIFY(!switcherEntry(treemapId)->isEnabled());
    // ... and that automatic fallback is no preference of the trash.
    QVERIFY(preferences()->preferredView(trash).isEmpty());

    // A preference that the location does not support falls back quietly, too.
    preferences()->setPreferredView(trash, treemapId);
    navigate(container, url(QStringLiteral("c")));
    QTest::failOnWarning(whaleWarnings());
    navigate(container, trash);
    QVERIFY(host->isDolphinViewShown());
    QCOMPARE(host->viewOrigin(), QStringLiteral("fallback"));
    QVERIFY(host->lastRestore().fallback);
    QCOMPARE(preferences()->preferredView(trash), treemapId);

    // Back to A by a new navigation: the treemap again.
    navigate(container, url(QStringLiteral("a")));
    QCOMPARE(host->currentViewId(), treemapId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("preference"));
    QVERIFY(switcherEntry(treemapId)->isEnabled());
}

void WhaleViewPreferencesTest::testMissingPlugin()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(choose(treemapId));
    navigate(container, url(QStringLiteral("b")));
    QVERIFY(choose(fileListId));

    // The treemap plugin goes missing.
    registry()->setPluginAvailable(treemapId, false);
    QTest::failOnWarning(whaleWarnings());
    for (int i = 0; i < 3; ++i) {
        navigate(container, url(QStringLiteral("a")));
        QVERIFY(host->isDolphinViewShown());
        QCOMPARE(host->viewOrigin(), QStringLiteral("fallback"));
        QCOMPARE(host->lastRestore().requestedViewId, treemapId);
        QVERIFY(host->lastRestore().fallback);
        QTRY_VERIFY(container->view()->itemsCount() > 0);
        // The preference stays for when the plugin is back.
        QCOMPARE(preferences()->preferredView(url(QStringLiteral("a"))), treemapId);
        navigate(container, url(QStringLiteral("b")));
    }
    QVERIFY(!switcherEntry(treemapId)->isEnabled());

    // Back again: A shows the treemap on the next visit.
    registry()->setPluginAvailable(treemapId, true);
    navigate(container, url(QStringLiteral("a")));
    QCOMPARE(host->currentViewId(), treemapId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("preference"));
}

void WhaleViewPreferencesTest::testHistoryHasPriority()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    KActionCollection *ac = m_mainWindow->actionCollection();
    QVERIFY(choose(treemapId));
    navigate(container, url(QStringLiteral("b")));
    QVERIFY(choose(fileListId));

    // Meanwhile (e.g. in another window) A prefers the test list, B the treemap.
    preferences()->setPreferredView(url(QStringLiteral("a")), testListId);
    preferences()->setPreferredView(url(QStringLiteral("b")), treemapId);

    // Back to A: the view as it was there, not the preference.
    ac->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QCOMPARE(host->currentViewId(), treemapId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("history"));
    QCOMPARE(host->lastRestore().source, QStringLiteral("history"));

    // Forward to B: the file list as it was.
    ac->action(KStandardAction::name(KStandardAction::Forward))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("b")));
    QVERIFY(host->isDolphinViewShown());
    QCOMPARE(host->viewOrigin(), QStringLiteral("history"));

    // A new navigation uses the preferences again.
    navigate(container, url(QStringLiteral("a")));
    QCOMPARE(host->currentViewId(), testListId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("preference"));
    navigate(container, url(QStringLiteral("b")));
    QCOMPARE(host->currentViewId(), treemapId);
}

void WhaleViewPreferencesTest::testSessionHasPriority()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    QVERIFY(choose(treemapId));

    KConfig config(m_tmpHome.filePath(QStringLiteral("session")), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Window"));
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->saveProperties(group);
    m_mainWindow.reset();

    // The preference of A changes before the restart.
    preferences()->setPreferredView(url(QStringLiteral("a")), testListId);

    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow->resize(1000, 700);
    // As KMainWindow::restore(): the session first, then the window is shown. A shown window
    // without a tab must not get events (e.g. a clipboard change on Wayland: Dolphin's Paste
    // action asks the active view area, which does not exist yet).
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->readProperties(group);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_mainWindow.data()));
    container = m_mainWindow->activeViewContainer();
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    WhaleViewHost *host = container->viewHost();
    // The session's view, not the preference.
    QCOMPARE(host->currentViewId(), treemapId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("session"));
    QTest::qWait(50); // nothing pending replaces it later
    QCOMPARE(host->currentViewId(), treemapId);

    // After that, new navigation uses the preference.
    navigate(container, url(QStringLiteral("b")));
    navigate(container, url(QStringLiteral("a")));
    QCOMPARE(host->currentViewId(), testListId);
    QCOMPARE(host->viewOrigin(), QStringLiteral("preference"));
}

void WhaleViewPreferencesTest::testSplitView()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    QTRY_VERIFY(page->secondaryViewContainer());
    DolphinViewContainer *left = page->primaryViewContainer();
    DolphinViewContainer *right = page->secondaryViewContainer();
    QTRY_VERIFY(right->isActive());

    // Left: A with the treemap. Right: B with the file list, both chosen by the user.
    left->setActive(true);
    QVERIFY(choose(treemapId));
    right->setActive(true);
    navigate(right, url(QStringLiteral("b")));
    QVERIFY(right->viewHost()->isDolphinViewShown());
    // (A mode of the file list chooses the file list when another view is shown.)
    QVERIFY(choose(testListId));
    QVERIFY(choose(fileListId));
    QCOMPARE(preferences()->preferredView(url(QStringLiteral("b"))), fileListId);
    QCOMPARE(left->viewHost()->currentViewId(), treemapId);

    // Crossing over: each side shows the other location's view.
    navigate(left, url(QStringLiteral("b")));
    navigate(right, url(QStringLiteral("a")));
    QVERIFY(left->viewHost()->isDolphinViewShown());
    QCOMPARE(right->viewHost()->currentViewId(), treemapId);
    QCOMPARE(left->viewHost()->viewOrigin(), QStringLiteral("preference"));
    QCOMPARE(right->viewHost()->viewOrigin(), QStringLiteral("preference"));
    // And back.
    navigate(left, url(QStringLiteral("a")));
    navigate(right, url(QStringLiteral("b")));
    QCOMPARE(left->viewHost()->currentViewId(), treemapId);
    QVERIFY(right->viewHost()->isDolphinViewShown());

    // Both sides at A share its preference; the right side's choice there is A's new one.
    navigate(right, url(QStringLiteral("a")));
    QCOMPARE(right->viewHost()->currentViewId(), treemapId);
    QVERIFY(right->isActive());
    QVERIFY(choose(testListId));
    QCOMPARE(left->viewHost()->currentViewId(), treemapId); // shown views are not switched
    navigate(left, url(QStringLiteral("c")));
    navigate(left, url(QStringLiteral("a")));
    QCOMPARE(left->viewHost()->currentViewId(), testListId);
}

void WhaleViewPreferencesTest::testNewViewArea()
{
    // The first location of a new window, tab or split view uses its preference, too.
    preferences()->setPreferredView(url(QStringLiteral("a")), treemapId);
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    QTRY_COMPARE(container->viewHost()->currentViewId(), treemapId);
    QCOMPARE(container->viewHost()->viewOrigin(), QStringLiteral("preference"));

    auto tabWidget = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    tabWidget->openNewActivatedTab(url(QStringLiteral("b")));
    QTRY_COMPARE(m_mainWindow->activeViewContainer()->url(), url(QStringLiteral("b")));
    QCOMPARE(m_mainWindow->activeViewContainer()->viewHost()->viewOrigin(), QStringLiteral("kept"));
    QVERIFY(m_mainWindow->activeViewContainer()->viewHost()->isDolphinViewShown());

    tabWidget->openNewActivatedTab(url(QStringLiteral("a")));
    QTRY_COMPARE(m_mainWindow->activeViewContainer()->url(), url(QStringLiteral("a")));
    QTRY_COMPARE(m_mainWindow->activeViewContainer()->viewHost()->currentViewId(), treemapId);

    // A tab opened in the background shows the preferred view once it is shown.
    tabWidget->openNewTab(url(QStringLiteral("a")));
    DolphinTabPage *background = tabWidget->tabPageAt(tabWidget->count() - 1);
    QVERIFY(background != tabWidget->currentTabPage());
    tabWidget->setCurrentIndex(tabWidget->count() - 1);
    QTRY_COMPARE(background->primaryViewContainer()->viewHost()->currentViewId(), treemapId);
}

void WhaleViewPreferencesTest::testClearPreference()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(choose(treemapId));
    navigate(container, url(QStringLiteral("b")));
    QVERIFY(choose(fileListId));

    QVERIFY(preferences()->clearPreferredView(url(QStringLiteral("a"))));
    navigate(container, url(QStringLiteral("a")));
    // The default: no preference, so the shown file list stays.
    QVERIFY(host->isDolphinViewShown());
    QCOMPARE(host->viewOrigin(), QStringLiteral("kept"));
    QVERIFY(preferences()->preferredView(url(QStringLiteral("a"))).isEmpty());
}

void WhaleViewPreferencesTest::testFileListModes()
{
    // O3 remembers the file list as one view; its mode stays Dolphin's (view properties).
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    KActionCollection *ac = m_mainWindow->actionCollection();
    QVERIFY(choose(treemapId));

    // Choosing a view mode in the treemap chooses the file list for A.
    ac->action(QStringLiteral("details"))->trigger();
    QVERIFY(host->isDolphinViewShown());
    QCOMPARE(container->view()->viewMode(), DolphinView::DetailsView);
    QCOMPARE(preferences()->preferredView(url(QStringLiteral("a"))), fileListId);

    // Switching modes in the file list is no choice of a view type.
    navigate(container, url(QStringLiteral("b")));
    ac->action(QStringLiteral("icons"))->trigger();
    QCOMPARE(container->view()->viewMode(), DolphinView::IconsView);
    QVERIFY(preferences()->preferredView(url(QStringLiteral("b"))).isEmpty());
    ac->action(QStringLiteral("compact"))->trigger();
    QCOMPARE(container->view()->viewMode(), DolphinView::CompactView);
    QCOMPARE(preferences()->count(), 1);

    // The file's entries hold view IDs only, never a mode.
    KConfig file(preferencesFile(), KConfig::SimpleConfig);
    const KConfigGroup group = file.group(QStringLiteral("PreferredViews"));
    for (const QString &key : group.keyList()) {
        QCOMPARE(group.readEntry(key, QStringList()).value(0), fileListId);
    }
}

QTEST_MAIN(WhaleViewPreferencesTest)

#include "whaleviewpreferencestest.moc"
