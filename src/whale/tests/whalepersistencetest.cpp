/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

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

#include <QDataStream>
#include <QIODevice>
#include <QListView>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTest>

namespace
{
const QString testListId = QStringLiteral("whale_testlist");
const QString helloId = QStringLiteral("whale_hello");
const QString dynamicId = QStringLiteral("whale_test_dynamic");

QString topItem(QWidget *widget)
{
    auto listView = qobject_cast<QListView *>(widget);
    return listView ? listView->indexAt(QPoint(1, 1)).data().toString() : QString();
}

QByteArray testListState(const QUrl &topItem)
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << quint8(1) << topItem;
    return state;
}
}

class WhalePersistenceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testLocationEnvelope();
    void testLegacyLocationState();
    void testDamagedData_data();
    void testDamagedData();
    void testSessionFormat();

    void testStateSurvivesViewSwitch();
    void testHistoryBackAndForward();
    void testStatePerViewAndLocation();
    void testSessionRestore();
    void testOldSessionWithoutWhaleData();
    void testMissingPluginFallsBack();
    void testCapabilitiesChanged();

private:
    DolphinViewContainer *showMainWindow(DolphinMainWindow *window, const QUrl &url);
    QWidget *showView(DolphinViewContainer *container, const QString &viewId);
    void scrollToTop(QWidget *listWidget, const QString &name);
    QUrl url(const QString &name) const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

void WhalePersistenceTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_DYNAMIC_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhalePersistenceTest::init()
{
    m_dir.reset(new TestDir());
    // Enough items to scroll.
    for (int i = 0; i < 120; ++i) {
        m_dir->createFile(QStringLiteral("f%1.txt").arg(i, 3, 10, QLatin1Char('0')));
    }
    m_dir->createFile(QStringLiteral("a/x.txt"));
    m_dir->createFile(QStringLiteral("b-zoom/y.txt"));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhalePersistenceTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
}

DolphinViewContainer *WhalePersistenceTest::showMainWindow(DolphinMainWindow *window, const QUrl &url)
{
    window->openDirectories({url}, false);
    window->resize(1000, 600);
    window->show();
    if (!QTest::qWaitForWindowExposed(window)) {
        return nullptr;
    }
    DolphinViewContainer *container = window->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    container->view()->setSortRole("text");
    container->view()->setSortOrder(Qt::AscendingOrder);
    container->view()->setSortFoldersFirst(true);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() > 0;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QWidget *WhalePersistenceTest::showView(DolphinViewContainer *container, const QString &viewId)
{
    auto actions = container->window()->findChild<WhaleViewActions *>();
    WhaleViewPlugin *plugin = actions->registry()->plugin(viewId);
    if (!plugin || !container->viewHost()->showPluginView(viewId, plugin)) {
        return nullptr;
    }
    return container->viewHost()->currentView()->widget();
}

void WhalePersistenceTest::scrollToTop(QWidget *listWidget, const QString &name)
{
    auto listView = qobject_cast<QListView *>(listWidget);
    QVERIFY(listView);
    QTRY_VERIFY(listView->model()->rowCount() > 100);
    for (int row = 0; row < listView->model()->rowCount(); ++row) {
        if (listView->model()->index(row, 0).data().toString() == name) {
            listView->scrollTo(listView->model()->index(row, 0), QAbstractItemView::PositionAtTop);
            QTRY_COMPARE(topItem(listView), name);
            return;
        }
    }
    QFAIL(qPrintable(name));
}

QUrl WhalePersistenceTest::url(const QString &name) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + name);
}

void WhalePersistenceTest::testLocationEnvelope()
{
    const QByteArray dolphinState("dolphin's own bytes");
    WhaleViewsState views;
    views.activeViewId = testListId;
    views.states.insert(testListId, QByteArray("list"));
    views.states.insert(helloId, QByteArray("hello"));

    const QByteArray wrapped = WhalePersistence::wrapLocationState(dolphinState, views);
    QByteArray dolphin;
    WhaleViewsState read;
    QVERIFY(WhalePersistence::unwrapLocationState(wrapped, &dolphin, &read));
    QCOMPARE(dolphin, dolphinState);
    QCOMPARE(read, views);
    // Deterministic bytes for equal states.
    QCOMPARE(WhalePersistence::wrapLocationState(dolphinState, read), wrapped);
}

void WhalePersistenceTest::testLegacyLocationState()
{
    // A DolphinView state as Dolphin (and Whale before M6) wrote it.
    DolphinViewContainer *container = showMainWindow(m_mainWindow.data(), m_dir->url());
    QVERIFY(container);
    QByteArray legacy;
    QDataStream stream(&legacy, QIODevice::WriteOnly);
    container->view()->saveState(stream);
    QVERIFY(!legacy.isEmpty());

    QByteArray dolphin;
    WhaleViewsState views;
    views.activeViewId = QStringLiteral("untouched");
    QVERIFY(!WhalePersistence::unwrapLocationState(legacy, &dolphin, &views));
    QCOMPARE(dolphin, legacy);
    QCOMPARE(views.activeViewId, QStringLiteral("untouched"));
}

void WhalePersistenceTest::testDamagedData_data()
{
    QTest::addColumn<QByteArray>("data");
    const QByteArray good = WhalePersistence::wrapLocationState("x", WhaleViewsState{testListId, {{testListId, "s"}}});
    QByteArray newer = good;
    newer[11] = char(99); // the format version (after the 8 magic bytes, big endian)
    QTest::newRow("truncated") << good.left(good.size() - 3);
    QTest::newRow("newer format") << newer;
    QTest::newRow("only the magic") << good.left(8);
    QTest::newRow("garbage after the magic") << (good.left(8) + QByteArray(40, '\xff'));
}

void WhalePersistenceTest::testDamagedData()
{
    QFETCH(QByteArray, data);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Ignoring")));
    QByteArray dolphin("before");
    WhaleViewsState views;
    QVERIFY(!WhalePersistence::unwrapLocationState(data, &dolphin, &views));
    QVERIFY(dolphin.isEmpty()); // never hand damaged bytes to the DolphinView
    QCOMPARE(views, WhaleViewsState());

    // The same for session data.
    QByteArray session = WhalePersistence::writeSessionState({WhaleViewsState{testListId, {{testListId, "s"}}}});
    session.chop(2);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Ignoring")));
    QVERIFY(WhalePersistence::readSessionState(session).isEmpty());
}

void WhalePersistenceTest::testSessionFormat()
{
    const QList<WhaleViewsState> panes = {WhaleViewsState{testListId, {{testListId, "left"}}}, WhaleViewsState{WhaleViewHost::dolphinViewId(), {}}};
    QCOMPARE(WhalePersistence::readSessionState(WhalePersistence::writeSessionState(panes)), panes);
    QVERIFY(WhalePersistence::readSessionState(QByteArray()).isEmpty());
}

void WhalePersistenceTest::testStateSurvivesViewSwitch()
{
    DolphinViewContainer *container = showMainWindow(m_mainWindow.data(), m_dir->url());
    QVERIFY(container);
    QWidget *list = showView(container, testListId);
    scrollToTop(list, QStringLiteral("f060.txt"));

    container->viewHost()->showDolphinView();
    list = showView(container, testListId);
    QTRY_COMPARE(topItem(list), QStringLiteral("f060.txt"));
}

void WhalePersistenceTest::testHistoryBackAndForward()
{
    DolphinViewContainer *container = showMainWindow(m_mainWindow.data(), m_dir->url());
    QVERIFY(container);
    KActionCollection *ac = m_mainWindow->actionCollection();

    // A: the test list, scrolled.
    QWidget *list = showView(container, testListId);
    scrollToTop(list, QStringLiteral("f080.txt"));

    // B: the file list.
    container->setUrl(url(QStringLiteral("a")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QCOMPARE(container->viewHost()->currentViewId(), testListId); // navigating keeps the view
    container->viewHost()->showDolphinView();

    // Back: the test list again, at the same place.
    ac->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->viewHost()->currentViewId(), testListId);
    QCOMPARE(container->viewHost()->lastRestore().source, QStringLiteral("history"));
    QVERIFY(!container->viewHost()->lastRestore().fallback);
    list = container->viewHost()->currentView()->widget();
    QTRY_COMPARE(topItem(list), QStringLiteral("f080.txt"));

    // Forward: the file list.
    ac->action(KStandardAction::name(KStandardAction::Forward))->trigger();
    QTRY_VERIFY(container->viewHost()->isDolphinViewShown());
    QCOMPARE(container->url(), url(QStringLiteral("a")));

    // Back once more: the state was saved again when leaving.
    ac->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->viewHost()->currentViewId(), testListId);
    QTRY_COMPARE(topItem(container->viewHost()->currentView()->widget()), QStringLiteral("f080.txt"));
}

void WhalePersistenceTest::testStatePerViewAndLocation()
{
    DolphinViewContainer *container = showMainWindow(m_mainWindow.data(), m_dir->url());
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();

    // Two views at one location, each with its own state.
    scrollToTop(showView(container, testListId), QStringLiteral("f050.txt"));
    QVERIFY(showView(container, dynamicId));
    host->currentView()->restoreState("zoom");
    const WhaleViewsState here = host->viewsState();
    QCOMPARE(here.activeViewId, dynamicId);
    QCOMPARE(here.states.value(dynamicId), QByteArray("zoom"));
    QCOMPARE(here.states.value(testListId), testListState(url(QStringLiteral("f050.txt"))));

    // The test list gets its own state back, not the other view's.
    scrollToTop(showView(container, testListId), QStringLiteral("f050.txt"));
    QCOMPARE(host->viewsState().states.value(dynamicId), QByteArray("zoom"));

    // Another location starts without states.
    container->setUrl(url(QStringLiteral("a")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QTRY_COMPARE(host->viewsState().states.keys(), QList<QString>({testListId}));
    QVERIFY(host->viewsState().states.value(testListId) != testListState(url(QStringLiteral("f050.txt"))));
}

void WhalePersistenceTest::testSessionRestore()
{
    DolphinViewContainer *container = showMainWindow(m_mainWindow.data(), m_dir->url());
    QVERIFY(container);
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    QTRY_VERIFY(page->secondaryViewContainer());
    DolphinViewContainer *left = page->primaryViewContainer();
    DolphinViewContainer *right = page->secondaryViewContainer();
    QTRY_VERIFY(right->view()->itemsCount() > 100);

    // Left: test list scrolled to f070. Right: the dynamic view with its state.
    scrollToTop(showView(left, testListId), QStringLiteral("f070.txt"));
    QVERIFY(showView(right, dynamicId));
    right->viewHost()->currentView()->restoreState("zoom");

    KConfig config(m_tmpHome.filePath(QStringLiteral("session")), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Window"));
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->saveProperties(group);
    // Dolphin's own entries are untouched; Whale's key sits next to them.
    const KConfigGroup tab = group.group(QStringLiteral("Tabs")).group(QStringLiteral("0"));
    QVERIFY(tab.hasKey("State"));
    QVERIFY(tab.hasKey(WhalePersistence::SessionKey));
    m_mainWindow.reset();

    // A new window reads the session.
    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow->resize(1000, 600);
    // As KMainWindow::restore(): the session first, then the window is shown. A shown window
    // without a tab must not get events (e.g. a clipboard change on Wayland: Dolphin's Paste
    // action asks the active view area, which does not exist yet).
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->readProperties(group);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_mainWindow.data()));
    page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    QVERIFY(page->splitViewEnabled());
    left = page->primaryViewContainer();
    right = page->secondaryViewContainer();
    QCOMPARE(left->viewHost()->currentViewId(), testListId);
    QCOMPARE(right->viewHost()->currentViewId(), dynamicId);
    QCOMPARE(left->viewHost()->lastRestore().source, QStringLiteral("session"));
    QTRY_COMPARE(topItem(left->viewHost()->currentView()->widget()), QStringLiteral("f070.txt"));
    QCOMPARE(right->viewHost()->currentView()->saveState(), QByteArray("zoom"));
}

void WhalePersistenceTest::testOldSessionWithoutWhaleData()
{
    // A session as Dolphin (or Whale before M6) wrote it: no Whale key.
    DolphinViewContainer *container = showMainWindow(m_mainWindow.data(), m_dir->url());
    QVERIFY(container);
    KConfig config(m_tmpHome.filePath(QStringLiteral("old-session")), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Window"));
    auto tabWidget = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    tabWidget->saveProperties(group);
    KConfigGroup tab = group.group(QStringLiteral("Tabs")).group(QStringLiteral("0"));
    tab.deleteEntry(WhalePersistence::SessionKey);

    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*Whale.*")));
    m_mainWindow.reset(new DolphinMainWindow());
    // As KMainWindow::restore(): the session first, then the window is shown. A shown window
    // without a tab must not get events (e.g. a clipboard change on Wayland: Dolphin's Paste
    // action asks the active view area, which does not exist yet).
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->readProperties(group);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_mainWindow.data()));
    container = m_mainWindow->activeViewContainer();
    QVERIFY(container->viewHost()->isDolphinViewShown());
    QTRY_VERIFY(container->view()->itemsCount() > 100);
    QVERIFY(container->viewHost()->lastRestore().source.isEmpty());
}

void WhalePersistenceTest::testMissingPluginFallsBack()
{
    DolphinViewContainer *container = showMainWindow(m_mainWindow.data(), m_dir->url());
    QVERIFY(container);
    // A session whose view comes from a plugin that is not there (any more).
    const QByteArray missingState("state of a missing plugin");
    const QByteArray session =
        WhalePersistence::writeSessionState({WhaleViewsState{QStringLiteral("whale_not_installed"), {{QStringLiteral("whale_not_installed"), missingState}}}});
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("whale_not_installed")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Cannot restore the view")));
    WhaleViewHost::restoreSessionState({container->viewHost()}, session);

    WhaleViewHost *host = container->viewHost();
    QVERIFY(host->isDolphinViewShown());
    QVERIFY(host->lastRestore().fallback);
    QCOMPARE(host->lastRestore().requestedViewId, QStringLiteral("whale_not_installed"));
    QCOMPARE(host->lastRestore().shownViewId, WhaleViewHost::dolphinViewId());
    // The file list works, and the missing plugin's state is kept for later.
    QVERIFY(container->view()->itemsCount() > 100);
    QCOMPARE(host->viewsState().states.value(QStringLiteral("whale_not_installed")), missingState);
    const QList<WhaleViewsState> saved = WhalePersistence::readSessionState(WhaleViewHost::saveSessionState({host}));
    QCOMPARE(saved.first().states.value(QStringLiteral("whale_not_installed")), missingState);
    QCOMPARE(saved.first().activeViewId, WhaleViewHost::dolphinViewId());

    // The history keeps working.
    container->setUrl(url(QStringLiteral("a")));
    QTRY_COMPARE(container->view()->itemsCount(), 1);
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_VERIFY(container->view()->itemsCount() > 100);
    QVERIFY(host->isDolphinViewShown());
}

void WhalePersistenceTest::testCapabilitiesChanged()
{
    DolphinViewContainer *container = showMainWindow(m_mainWindow.data(), m_dir->url());
    QVERIFY(container);
    QAction *zoomIn = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::ZoomIn));
    QVERIFY(showView(container, dynamicId));
    QVERIFY(!zoomIn->isEnabled());

    // The state makes the view zoomable: the gate opens at once.
    container->viewHost()->currentView()->restoreState("zoom");
    QVERIFY(zoomIn->isEnabled());
    container->viewHost()->currentView()->restoreState(QByteArray());
    QVERIFY(!zoomIn->isEnabled());

    // So does a location.
    container->setUrl(url(QStringLiteral("b-zoom")));
    QTRY_VERIFY(zoomIn->isEnabled());
    container->setUrl(m_dir->url());
    QTRY_VERIFY(!zoomIn->isEnabled());

    // And a restore from the history: back to b-zoom.
    container->setUrl(url(QStringLiteral("b-zoom")));
    QTRY_VERIFY(zoomIn->isEnabled());
}

QTEST_MAIN(WhalePersistenceTest)

#include "whalepersistencetest.moc"
