/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalestylemanager.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalestyles.h"
#include "whaletoolbarsettings.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KLocalizedString>
#include <KToolBar>
#include <KXMLGUIFactory>

#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStandardPaths>
#include <QTest>

/**
 * Applying, capturing and the actions of styles in main windows (ST3).
 */
class WhaleStyleManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testActionsWithoutStyles();
    void testSaveAddsActionInAllWindows();
    void testSaveAgainKeepsIdShortcutAndSwitches();
    void testSwitchBetweenSchlichtAndBilder();
    void testShortcutsFromConfigureShortcuts();
    void testStylesAfterRestart();
    void testAppliedStyleIsStartOfNextWindow();

private:
    DolphinMainWindow *openWindow();
    static DolphinTabPage *tabPage(DolphinMainWindow *window);
    static QStringList menuEntries(DolphinMainWindow *window);
    static QDockWidget *dock(DolphinMainWindow *window, const char *name);
    // Sets the window up as "Schlicht" or "Bilder".
    static void makeSchlicht(DolphinMainWindow *window);
    static void makeBilder(DolphinMainWindow *window);

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QList<DolphinMainWindow *> m_windows;
};

void WhaleStyleManagerTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    KLocalizedString::setApplicationDomain({"whale"});
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->setGlobalViewProps(true);
    GeneralSettings::self()->save();
}

void WhaleStyleManagerTest::init()
{
    // The last split view decides about new windows here (E3).
    GeneralSettings::setSplitViewAtStartup(GeneralSettings::EnumSplitViewAtStartup::LastState);
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"));
    m_dir->createFile(QStringLiteral("b.txt"));
}

void WhaleStyleManagerTest::cleanup()
{
    qDeleteAll(m_windows);
    m_windows.clear();
    m_dir.reset();
    WhaleStyleStore &store = WhaleStyles::self()->editableStore();
    const QStringList ids = store.ids();
    for (const QString &id : ids) {
        store.removeStyle(id);
    }
    WhaleStyles::self()->save();
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    TestHelpers::openNewWindowsInSingleView();
    GeneralSettings::self()->save();
}

DolphinMainWindow *WhaleStyleManagerTest::openWindow()
{
    auto window = new DolphinMainWindow();
    m_windows.append(window);
    window->openDirectories({m_dir->url()}, GeneralSettings::splitViewAtStartup() == GeneralSettings::EnumSplitViewAtStartup::AlwaysSplit);
    window->resize(1000, 700);
    window->show();
    if (!QTest::qWaitForWindowExposed(window)) {
        return nullptr;
    }
    DolphinViewContainer *container = window->activeViewContainer();
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 2;
            },
            5000)) {
        return nullptr;
    }
    return window;
}

DolphinTabPage *WhaleStyleManagerTest::tabPage(DolphinMainWindow *window)
{
    return window->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
}

QStringList WhaleStyleManagerTest::menuEntries(DolphinMainWindow *window)
{
    auto menu = qobject_cast<KActionMenu *>(window->actionCollection()->action(WhaleStyleManager::menuActionName()));
    QStringList entries;
    const auto actions = menu->menu()->actions();
    for (const QAction *action : actions) {
        entries.append(action->isSeparator() ? QStringLiteral("--") : action->objectName());
    }
    return entries;
}

QDockWidget *WhaleStyleManagerTest::dock(DolphinMainWindow *window, const char *name)
{
    return window->findChild<QDockWidget *>(QLatin1String(name));
}

void WhaleStyleManagerTest::makeSchlicht(DolphinMainWindow *window)
{
    tabPage(window)->setSplitViewEnabled(false, WithoutAnimation);
    tabPage(window)->primaryViewContainer()->view()->setViewMode(DolphinView::CompactView);
    tabPage(window)->primaryViewContainer()->view()->setPreviewsShown(false);
    QAction *menuBar = window->actionCollection()->action(QStringLiteral("options_show_menubar"));
    if (menuBar->isChecked()) {
        menuBar->trigger();
    }
    window->toolBar(QStringLiteral("mainToolBar"))->hide();
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    GeneralSettings::setShowStatusBar(GeneralSettings::EnumShowStatusBar::FullWidth);
    GeneralSettings::self()->save();
    for (const char *name : {"placesDock", "foldersDock", "infoDock", "terminalDock"}) {
        if (QDockWidget *d = dock(window, name)) {
            d->hide();
        }
    }
}

void WhaleStyleManagerTest::makeBilder(DolphinMainWindow *window)
{
    tabPage(window)->setSplitViewEnabled(false, WithoutAnimation);
    tabPage(window)->primaryViewContainer()->view()->setViewMode(DolphinView::IconsView);
    tabPage(window)->primaryViewContainer()->view()->setPreviewsShown(true);
    QAction *menuBar = window->actionCollection()->action(QStringLiteral("options_show_menubar"));
    if (!menuBar->isChecked()) {
        menuBar->trigger();
    }
    window->toolBar(QStringLiteral("mainToolBar"))->show();
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    GeneralSettings::setShowStatusBar(GeneralSettings::EnumShowStatusBar::Small);
    GeneralSettings::self()->save();
    dock(window, "placesDock")->show();
    dock(window, "infoDock")->show();
}

void WhaleStyleManagerTest::testActionsWithoutStyles()
{
    DolphinMainWindow *window = openWindow();
    QVERIFY(window);
    QVERIFY(window->actionCollection()->action(WhaleStyleManager::menuActionName()));
    QVERIFY(window->actionCollection()->action(WhaleStyleManager::saveActionName()));
    QCOMPARE(menuEntries(window), QStringList{WhaleStyleManager::saveActionName()});
    // In the View menu, placed by the XML GUI file.
    auto viewMenu = qobject_cast<QMenu *>(window->guiFactory()->container(QStringLiteral("view"), window));
    QVERIFY(viewMenu);
    QVERIFY(viewMenu->actions().contains(window->actionCollection()->action(WhaleStyleManager::menuActionName())));
    QVERIFY(!WhaleStyleManager::ensure(window)->applyStyle(QStringLiteral("nope")));
    QVERIFY(WhaleStyleManager::ensure(window)->saveCurrentAsStyle(QStringLiteral("  ")).isEmpty());
}

void WhaleStyleManagerTest::testSaveAddsActionInAllWindows()
{
    DolphinMainWindow *first = openWindow();
    DolphinMainWindow *second = openWindow();
    QVERIFY(first && second);

    QCOMPARE(WhaleStyleManager::ensure(first)->saveCurrentAsStyle(QStringLiteral("Bilder & mehr")), QStringLiteral("bilder-mehr"));
    const QString actionName = WhaleStyleManager::actionName(QStringLiteral("bilder-mehr"));
    for (DolphinMainWindow *window : {first, second}) {
        QAction *action = window->actionCollection()->action(actionName);
        QVERIFY(action);
        QCOMPARE(action->text(), QStringLiteral("Bilder && mehr"));
        QCOMPARE(menuEntries(window), (QStringList{actionName, QStringLiteral("--"), WhaleStyleManager::saveActionName()}));
    }

    // Saved at once, with all elements available in the window: all but the filter of a right view.
    WhaleStyleStore reread(WhaleStyleStore::defaultConfig());
    const WhaleStyle *style = reread.style(QStringLiteral("bilder-mehr"));
    QVERIFY(style);
    QCOMPARE(style->elements.size(), 10);
    QVERIFY(!style->elements.contains(QStringLiteral("filterBar.right")));
    for (const WhaleStyleElementState &state : style->elements) {
        QVERIFY(state.enabled);
    }
}

void WhaleStyleManagerTest::testSaveAgainKeepsIdShortcutAndSwitches()
{
    DolphinMainWindow *window = openWindow();
    QVERIFY(window);
    WhaleStyleManager *manager = WhaleStyleManager::ensure(window);
    const QString id = manager->saveCurrentAsStyle(QStringLiteral("Schlicht"));

    WhaleStyle style = *WhaleStyles::self()->store().style(id);
    style.shortcut = QKeySequence(QStringLiteral("Ctrl+Alt+1"));
    style.elements[QStringLiteral("panels")].enabled = false;
    WhaleStyles::self()->editableStore().updateStyle(style);
    WhaleStyles::self()->save();
    QCOMPARE(window->actionCollection()->action(WhaleStyleManager::actionName(id))->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+1")));

    tabPage(window)->primaryViewContainer()->view()->setViewMode(DolphinView::DetailsView);
    QCOMPARE(manager->saveCurrentAsStyle(QStringLiteral(" schlicht ")), id);
    QCOMPARE(WhaleStyles::self()->store().ids(), QStringList{id});
    const WhaleStyle *again = WhaleStyles::self()->store().style(id);
    QCOMPARE(again->name, QStringLiteral("Schlicht"));
    QCOMPARE(again->shortcut, QKeySequence(QStringLiteral("Ctrl+Alt+1")));
    QVERIFY(!again->elements.value(QStringLiteral("panels")).enabled);
    QVERIFY(again->elements.value(QStringLiteral("viewStyle.left")).value.contains(QLatin1String("ViewMode=1"))); // DetailsView
}

void WhaleStyleManagerTest::testSwitchBetweenSchlichtAndBilder()
{
    DolphinMainWindow *window = openWindow();
    QVERIFY(window);
    WhaleStyleManager *manager = WhaleStyleManager::ensure(window);

    makeSchlicht(window);
    const QString schlicht = manager->saveCurrentAsStyle(QStringLiteral("Schlicht"));
    makeBilder(window);
    const QString bilder = manager->saveCurrentAsStyle(QStringLiteral("Bilder"));
    QCOMPARE(menuEntries(window),
             (QStringList{WhaleStyleManager::actionName(schlicht),
                          WhaleStyleManager::actionName(bilder),
                          QStringLiteral("--"),
                          WhaleStyleManager::saveActionName()}));

    DolphinView *view = tabPage(window)->primaryViewContainer()->view();
    for (int round = 0; round < 2; ++round) {
        // Through the actions, as the menu or a shortcut does.
        window->actionCollection()->action(WhaleStyleManager::actionName(schlicht))->trigger();
        QCOMPARE(view->viewMode(), DolphinView::CompactView);
        QVERIFY(!view->previewsShown());
        QTRY_VERIFY(window->menuBar()->isHidden());
        QVERIFY(window->toolBar(QStringLiteral("mainToolBar"))->isHidden());
        QVERIFY(!WhaleToolbarSettings::self()->fileViewToolBarsVisible());
        QCOMPARE(GeneralSettings::showStatusBar(), GeneralSettings::EnumShowStatusBar::FullWidth);
        QVERIFY(dock(window, "infoDock")->isHidden());
        QVERIFY(dock(window, "placesDock")->isHidden());

        window->actionCollection()->action(WhaleStyleManager::actionName(bilder))->trigger();
        QCOMPARE(view->viewMode(), DolphinView::IconsView);
        QVERIFY(view->previewsShown());
        QTRY_VERIFY(!window->menuBar()->isHidden());
        QVERIFY(!window->toolBar(QStringLiteral("mainToolBar"))->isHidden());
        QVERIFY(WhaleToolbarSettings::self()->fileViewToolBarsVisible());
        QCOMPARE(GeneralSettings::showStatusBar(), GeneralSettings::EnumShowStatusBar::Small);
        QVERIFY(!dock(window, "infoDock")->isHidden());
        QVERIFY(!dock(window, "placesDock")->isHidden());
    }

    // A disabled element is left as it is.
    WhaleStyle style = *WhaleStyles::self()->store().style(schlicht);
    style.elements[QStringLiteral("panels")].enabled = false;
    WhaleStyles::self()->editableStore().updateStyle(style);
    WhaleStyles::self()->save();
    QVERIFY(manager->applyStyle(schlicht));
    QVERIFY(!dock(window, "infoDock")->isHidden());
    QCOMPARE(view->viewMode(), DolphinView::CompactView);
}

void WhaleStyleManagerTest::testShortcutsFromConfigureShortcuts()
{
    DolphinMainWindow *first = openWindow();
    DolphinMainWindow *second = openWindow();
    QVERIFY(first && second);
    const QString id = WhaleStyleManager::ensure(first)->saveCurrentAsStyle(QStringLiteral("Bilder"));
    const QString name = WhaleStyleManager::actionName(id);

    // What Configure Shortcuts does on OK: changes the action and saves all shortcuts.
    QAction *action = first->actionCollection()->action(name);
    action->setShortcut(QKeySequence(QStringLiteral("Meta+B")));
    first->actionCollection()->writeSettings();
    const auto savedProperties = [first]() {
        return KXMLGUIFactory::readConfigFile(first->localXMLFile(), first->componentName());
    };
    QVERIFY(savedProperties().contains(name));

    WhaleStyleManager::ensure(first)->takeShortcuts();
    QCOMPARE(WhaleStyles::self()->store().style(id)->shortcut, QKeySequence(QStringLiteral("Meta+B")));
    QCOMPARE(WhaleStyleStore(WhaleStyleStore::defaultConfig()).style(id)->shortcut, QKeySequence(QStringLiteral("Meta+B")));
    QCOMPARE(second->actionCollection()->action(name)->shortcut(), QKeySequence(QStringLiteral("Meta+B")));
    QVERIFY(!savedProperties().contains(name));

    // Removed in Configure Shortcuts.
    first->actionCollection()->action(name)->setShortcut(QKeySequence());
    WhaleStyleManager::ensure(first)->takeShortcuts();
    QVERIFY(WhaleStyles::self()->store().style(id)->shortcut.isEmpty());
}

void WhaleStyleManagerTest::testStylesAfterRestart()
{
    DolphinMainWindow *window = openWindow();
    QVERIFY(window);
    const QString id = WhaleStyleManager::ensure(window)->saveCurrentAsStyle(QStringLiteral("Bilder"));
    WhaleStyle style = *WhaleStyles::self()->store().style(id);
    style.shortcut = QKeySequence(QStringLiteral("Ctrl+Alt+2"));
    WhaleStyles::self()->editableStore().updateStyle(style);
    WhaleStyles::self()->save();
    qDeleteAll(m_windows);
    m_windows.clear();

    // As a new process would: the file only.
    WhaleStyles::self()->editableStore().load();
    window = openWindow();
    QVERIFY(window);
    QAction *action = window->actionCollection()->action(WhaleStyleManager::actionName(id));
    QVERIFY(action);
    QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+2")));
}

void WhaleStyleManagerTest::testAppliedStyleIsStartOfNextWindow()
{
    DolphinMainWindow *window = openWindow();
    QVERIFY(window);
    WhaleStyleManager *manager = WhaleStyleManager::ensure(window);
    makeSchlicht(window);
    tabPage(window)->setSplitViewEnabled(true, WithoutAnimation);
    tabPage(window)->secondaryViewContainer()->view()->setViewMode(DolphinView::DetailsView);
    const QString zwei = manager->saveCurrentAsStyle(QStringLiteral("Zwei"));
    makeBilder(window);
    const QString bilder = manager->saveCurrentAsStyle(QStringLiteral("Bilder"));

    manager->applyStyle(zwei);
    QVERIFY(tabPage(window)->splitViewEnabled());
    QTRY_VERIFY(window->menuBar()->isHidden());
    QVERIFY(window->close());
    qDeleteAll(m_windows);
    m_windows.clear();

    window = openWindow();
    QVERIFY(window);
    QVERIFY(tabPage(window)->splitViewEnabled());
    QCOMPARE(tabPage(window)->primaryViewContainer()->view()->viewMode(), DolphinView::CompactView);
    QCOMPARE(tabPage(window)->secondaryViewContainer()->view()->viewMode(), DolphinView::DetailsView);
    QVERIFY(window->menuBar()->isHidden());
    QVERIFY(window->toolBar(QStringLiteral("mainToolBar"))->isHidden());
    QVERIFY(dock(window, "infoDock")->isHidden());
    QVERIFY(dock(window, "placesDock")->isHidden());

    WhaleStyleManager::ensure(window)->applyStyle(bilder);
    // DolphinMainWindow remembers the menu bar a moment later (queued, bug 286822).
    QTRY_VERIFY(!window->menuBar()->isHidden());
    QVERIFY(window->close());
    qDeleteAll(m_windows);
    m_windows.clear();

    window = openWindow();
    QVERIFY(window);
    QVERIFY(!tabPage(window)->splitViewEnabled());
    QCOMPARE(tabPage(window)->primaryViewContainer()->view()->viewMode(), DolphinView::IconsView);
    QVERIFY(!window->menuBar()->isHidden());
    QVERIFY(!window->toolBar(QStringLiteral("mainToolBar"))->isHidden());
    QVERIFY(!dock(window, "infoDock")->isHidden());
}

QTEST_MAIN(WhaleStyleManagerTest)

#include "whalestylemanagertest.moc"
