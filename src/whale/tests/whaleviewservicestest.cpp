/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewservices.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "views/dolphinviewactionhandler.h"
#include "whaletreecache.h"
#include "whaleviewactions.h"
#include "whaleviewcommandscope.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KLocalizedString>

#include <QDialog>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace
{
const QString treemapId = QStringLiteral("orcinus_treemap");
}

/**
 * The view services of a main window (WhaleViewServices) and action sets that share them:
 * the window's own (acting on the active view) and a second one bound to one view area, as
 * the File View Toolbar will create them (multi-toolbar plan, TB1).
 */
class WhaleViewServicesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testOnePerWindow();
    void testWindowsHaveOwnServices();
    void testServicesOwnRegistryAndCache();
    void testWindowActionsUseServices();
    void testSecondActionSetSharesServices();
    void testPluginAvailabilityIsShared();
    void testDeletingSecondActionSetKeepsServices();
    void testClosingWindowDestroysServices();
    void testHostNeedsNoActionSet();
    void testViewAreasShareTreeCache();
    void testBoundActionSetStaysOnItsHost();
    void testBoundActionSetWithoutShortcuts();

private:
    DolphinViewContainer *showMainWindow();
    void showSplit();
    WhaleViewServices *services() const;
    WhaleViewActions *windowActions() const;

    // A second action set bound to one view area: own collection, own handler, no shortcuts.
    struct BoundActionSet {
        QObject *owner = nullptr;
        KActionCollection *collection = nullptr;
        DolphinViewActionHandler *handler = nullptr;
        WhaleViewActions *actions = nullptr;
    };
    BoundActionSet createBoundActionSet(DolphinViewContainer *container);

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    DolphinViewContainer *m_left = nullptr;
    DolphinViewContainer *m_right = nullptr;
};

void WhaleViewServicesTest::initTestCase()
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
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleViewServicesTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(3000, 'a'));
    m_dir->createFile(QStringLiteral("sub/b.txt"), QByteArray(1000, 'b'));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleViewServicesTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
    m_left = m_right = nullptr;
}

DolphinViewContainer *WhaleViewServicesTest::showMainWindow()
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1000, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 2;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

void WhaleViewServicesTest::showSplit()
{
    QVERIFY(showMainWindow());
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    m_left = page->primaryViewContainer();
    m_right = page->secondaryViewContainer();
    QVERIFY(m_left && m_right);
    QTRY_COMPARE(m_right->view()->itemsCount(), 2);
}

WhaleViewServices *WhaleViewServicesTest::services() const
{
    return WhaleViewServices::of(m_mainWindow.data());
}

WhaleViewActions *WhaleViewServicesTest::windowActions() const
{
    return m_mainWindow->findChild<WhaleViewActions *>(QString(), Qt::FindDirectChildrenOnly);
}

WhaleViewServicesTest::BoundActionSet WhaleViewServicesTest::createBoundActionSet(DolphinViewContainer *container)
{
    BoundActionSet set;
    // Inside the view area, as a toolbar of the view area would hold it.
    set.owner = new QObject(container);
    set.collection = new KActionCollection(set.owner, QStringLiteral("whale-test-bound"));
    set.handler = new DolphinViewActionHandler(set.collection, nullptr, set.owner);
    set.handler->setCurrentView(container->view());
    set.actions = new WhaleViewActions(WhaleViewServices::of(container), set.collection, set.handler, set.owner);
    set.actions->setCurrentHost(container->viewHost());
    // setCurrentView() leaves the state of the actions to the owner (TB0).
    set.handler->updateViewActions();
    WhaleViewActions::clearShortcuts(set.collection);
    return set;
}

void WhaleViewServicesTest::testOnePerWindow()
{
    QVERIFY(services());
    QCOMPARE(m_mainWindow->findChildren<WhaleViewServices *>().count(), 1);
    QCOMPARE(services()->parent(), m_mainWindow.data());
    // ensure() hands out the existing services.
    QCOMPARE(WhaleViewServices::ensure(m_mainWindow.data()), services());
    QCOMPARE(m_mainWindow->findChildren<WhaleViewServices *>().count(), 1);
    // Every widget of the window finds them.
    QVERIFY(showMainWindow());
    QCOMPARE(WhaleViewServices::of(m_mainWindow->activeViewContainer()->viewHost()), services());
}

void WhaleViewServicesTest::testWindowsHaveOwnServices()
{
    DolphinMainWindow second;
    WhaleViewServices *secondServices = WhaleViewServices::of(&second);
    QVERIFY(secondServices);
    QVERIFY(secondServices != services());
    QVERIFY(secondServices->registry() != services()->registry());
    QVERIFY(secondServices->treeCache() != services()->treeCache());
}

void WhaleViewServicesTest::testServicesOwnRegistryAndCache()
{
    QCOMPARE(services()->registry()->parent(), services());
    QCOMPARE(services()->treeCache()->parent(), services());
    QCOMPARE(m_mainWindow->findChildren<WhaleViewRegistry *>().count(), 1);
    QCOMPARE(m_mainWindow->findChildren<WhaleTreeCache *>().count(), 1);
}

void WhaleViewServicesTest::testWindowActionsUseServices()
{
    QVERIFY(windowActions());
    QCOMPARE(m_mainWindow->findChildren<WhaleViewActions *>(QString(), Qt::FindDirectChildrenOnly).count(), 1);
    QCOMPARE(windowActions()->services(), services());
    QCOMPARE(windowActions()->registry(), services()->registry());
    QCOMPARE(windowActions()->treeCache(), services()->treeCache());
    // The action set owns no services of its own.
    QVERIFY(windowActions()->findChildren<WhaleViewRegistry *>().isEmpty());
    QVERIFY(windowActions()->findChildren<WhaleTreeCache *>().isEmpty());
}

void WhaleViewServicesTest::testSecondActionSetSharesServices()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    const BoundActionSet set = createBoundActionSet(m_right);
    QCOMPARE(set.actions->services(), services());
    QCOMPARE(set.actions->registry(), windowActions()->registry());
    QCOMPARE(set.actions->treeCache(), windowActions()->treeCache());
    // The window's set, the bound one of this test, and one per shown File View Toolbar.
    QCOMPARE(m_mainWindow->findChildren<WhaleViewActions *>().count(), 2 + m_mainWindow->findChildren<WhaleViewCommandScope *>().count());
    QCOMPARE(m_mainWindow->findChildren<WhaleViewRegistry *>().count(), 1);
    QCOMPARE(m_mainWindow->findChildren<WhaleTreeCache *>().count(), 1);
    QCOMPARE(m_mainWindow->findChildren<WhaleViewServices *>().count(), 1);
    // The plugin views are the same entries in both, as separate actions.
    QAction *windowEntry = windowActions()->viewModeAction(treemapId);
    QAction *boundEntry = set.actions->viewModeAction(treemapId);
    QVERIFY(windowEntry && boundEntry);
    QVERIFY(windowEntry != boundEntry);
    QCOMPARE(windowActions()->viewModeActions().count(), set.actions->viewModeActions().count());
}

void WhaleViewServicesTest::testPluginAvailabilityIsShared()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    const BoundActionSet set = createBoundActionSet(m_right);
    WhaleViewRegistry *registry = services()->registry();
    WhaleViewPlugin *plugin = registry->plugin(treemapId);
    QVERIFY(plugin);
    QCOMPARE(set.actions->registry()->plugin(treemapId), plugin);
    registry->setPluginAvailable(treemapId, false);
    QVERIFY(!windowActions()->registry()->plugin(treemapId));
    QVERIFY(!set.actions->registry()->plugin(treemapId));
    registry->setPluginAvailable(treemapId, true);
    QCOMPARE(set.actions->registry()->plugin(treemapId), plugin);
}

void WhaleViewServicesTest::testDeletingSecondActionSetKeepsServices()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    BoundActionSet set = createBoundActionSet(m_right);
    QPointer<WhaleViewServices> kept = services();
    QPointer<WhaleViewRegistry> registry = services()->registry();
    QPointer<WhaleTreeCache> cache = services()->treeCache();
    delete set.owner;
    QVERIFY(kept && registry && cache);
    QCOMPARE(windowActions()->registry(), registry.data());
    // The window's actions still work with them: show the treemap through its entry.
    m_right->setActive(true);
    QTRY_VERIFY(m_right->isActive());
    windowActions()->viewModeAction(treemapId)->trigger();
    QTRY_COMPARE(m_right->viewHost()->currentViewId(), treemapId);
}

void WhaleViewServicesTest::testClosingWindowDestroysServices()
{
    QVERIFY(showMainWindow());
    QPointer<WhaleViewServices> kept = services();
    QPointer<WhaleViewRegistry> registry = services()->registry();
    QPointer<WhaleTreeCache> cache = services()->treeCache();
    QPointer<WhaleViewActions> actions = windowActions();
    m_mainWindow.reset();
    QVERIFY(!kept);
    QVERIFY(!registry);
    QVERIFY(!cache);
    QVERIFY(!actions);
}

void WhaleViewServicesTest::testHostNeedsNoActionSet()
{
    // A window with view services but without any action set: the view area finds the
    // plugins and tells the window about a changed view through the services alone.
    // (No main window meanwhile: one that was never shown crashes in updatePasteAction() on
    // a clipboard change under Wayland, see DolphinMainWindow.)
    m_mainWindow.reset();
    QWidget window;
    WhaleViewServices *windowServices = WhaleViewServices::ensure(&window);
    auto view = new DolphinView(m_dir->url(), &window);
    auto host = new WhaleViewHost(view, &window);
    window.resize(600, 400);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.findChildren<WhaleViewActions *>().isEmpty());
    QSignalSpy viewChanged(windowServices, &WhaleViewServices::viewChanged);
    QVERIFY(host->selectView(treemapId));
    QCOMPARE(host->currentViewId(), treemapId);
    QVERIFY(viewChanged.count() >= 1);
    QTRY_VERIFY(windowServices->treeCache()->statistics().misses >= 1);

    // Without services (no main window) the host has no plugins, as before.
    QWidget bare;
    auto bareView = new DolphinView(m_dir->url(), &bare);
    auto bareHost = new WhaleViewHost(bareView, &bare);
    QVERIFY(!WhaleViewServices::of(bareHost));
    QVERIFY(!bareHost->selectView(treemapId));
}

void WhaleViewServicesTest::testViewAreasShareTreeCache()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    WhaleTreeCache *cache = services()->treeCache();
    WhaleViewPlugin *plugin = services()->registry()->plugin(treemapId);
    QVERIFY(m_left->viewHost()->showPluginView(treemapId, plugin));
    QTRY_COMPARE(cache->statistics().misses, 1);
    QTRY_COMPARE(cache->statistics().entries, 1);
    // The other view area's treemap gets the tree of the same location from the same cache.
    QVERIFY(m_right->viewHost()->showPluginView(treemapId, plugin));
    QTRY_COMPARE(cache->statistics().hits + cache->statistics().joins, 1);
    QCOMPARE(cache->statistics().misses, 1);
}

void WhaleViewServicesTest::testBoundActionSetStaysOnItsHost()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    const BoundActionSet set = createBoundActionSet(m_right);
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());

    // Right: treemap, left: file list. The bound set shows the right one, the window's the left.
    QVERIFY(m_right->viewHost()->showPluginView(treemapId, services()->registry()->plugin(treemapId)));
    QTRY_VERIFY(set.actions->viewModeAction(treemapId)->isChecked());
    QVERIFY(!windowActions()->viewModeAction(treemapId)->isChecked());
    QVERIFY(!set.collection->action(QStringLiteral("show_hidden_files"))->isEnabled()); // no HiddenFiles capability
    QVERIFY(m_mainWindow->actionCollection()->action(QStringLiteral("show_hidden_files"))->isEnabled());

    // Activating the views moves the window's set; the bound set stays on the right one.
    m_right->setActive(true);
    QTRY_VERIFY(windowActions()->viewModeAction(treemapId)->isChecked());
    m_left->setActive(true);
    QTRY_VERIFY(!windowActions()->viewModeAction(treemapId)->isChecked());
    QVERIFY(set.actions->viewModeAction(treemapId)->isChecked());

    // Its entries act on its own view area only.
    set.collection->action(QStringLiteral("details"))->trigger();
    QTRY_VERIFY(m_right->viewHost()->isDolphinViewShown());
    QVERIFY(m_left->viewHost()->isDolphinViewShown());
    QTRY_VERIFY(set.collection->action(QStringLiteral("show_hidden_files"))->isEnabled());
}

void WhaleViewServicesTest::testBoundActionSetWithoutShortcuts()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    m_left->view()->setPreviewsShown(true);
    const BoundActionSet set = createBoundActionSet(m_right);

    // updateViewActions() after the binding: the state of its own view.
    QCOMPARE(set.collection->action(QStringLiteral("show_preview"))->isChecked(), m_right->view()->previewsShown());
    QCOMPARE(set.collection->action(QStringLiteral("show_hidden_files"))->isChecked(), m_right->view()->hiddenFilesShown());

    const auto actions = set.collection->actions();
    for (QAction *action : actions) {
        QVERIFY2(action->shortcuts().isEmpty(), qPrintable(action->objectName()));
    }
    // The window's actions keep theirs.
    QVERIFY(!m_mainWindow->actionCollection()->action(QStringLiteral("show_hidden_files"))->shortcuts().isEmpty());

    // The bound actions in a visible widget, as in a toolbar: the shortcut still reaches the
    // window's action for the active view, without KXmlGui's "ambiguous shortcut" message.
    auto bar = new QWidget(m_right);
    bar->addActions(actions);
    bar->show();
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    bool dialogShown = false;
    QTimer::singleShot(500, this, [&dialogShown]() {
        const auto windows = QApplication::topLevelWidgets();
        for (QWidget *window : windows) {
            if (auto dialog = qobject_cast<QDialog *>(window); dialog && dialog->isVisible()) {
                dialogShown = true;
                dialog->reject();
            }
        }
    });
    const bool leftBefore = m_left->view()->hiddenFilesShown();
    const bool rightBefore = m_right->view()->hiddenFilesShown();
    QTest::keyClick(m_left->view(), Qt::Key_Period, Qt::AltModifier); // show_hidden_files
    QTRY_VERIFY(m_left->view()->hiddenFilesShown() != leftBefore);
    QCOMPARE(m_right->view()->hiddenFilesShown(), rightBefore);
    QTest::qWait(600);
    QVERIFY(!dialogShown);
}

QTEST_MAIN(WhaleViewServicesTest)

#include "whaleviewservicestest.moc"
