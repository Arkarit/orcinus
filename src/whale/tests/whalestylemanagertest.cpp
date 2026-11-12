/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
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
#include "whaledefaultstyles.h"
#include "whalestyleelement.h"
#include "whalestyleelements.h"
#include "whalestyles.h"
#include "whaletoolbarsettings.h"
#include "whaleviewhost.h"
#include "whaleviewpreferences.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
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
    void testSaveLastStyle();
    void testStyleWithoutShownViews();
    void testDefaultStyles();

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
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
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
    WhaleViewPreferences::instance()->setPreferredView(m_dir->url(), QString());
    m_dir.reset();
    WhaleStyleStore &store = WhaleStyles::self()->editableStore();
    const QStringList ids = store.ids();
    for (const QString &id : ids) {
        store.removeStyle(id);
    }
    WhaleStyles::self()->save();
    KSharedConfig::openStateConfig()->deleteGroup(QStringLiteral("Styles"));
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
    QCOMPARE(menuEntries(window), (QStringList{WhaleStyleManager::saveLastActionName(), WhaleStyleManager::saveActionName()}));
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
        QCOMPARE(menuEntries(window),
                 (QStringList{actionName, QStringLiteral("--"), WhaleStyleManager::saveLastActionName(), WhaleStyleManager::saveActionName()}));
    }

    // Saved at once, with all elements available in the window: all but the filter and the view
    // of a right view.
    WhaleStyleStore reread(WhaleStyleStore::defaultConfig());
    const WhaleStyle *style = reread.style(QStringLiteral("bilder-mehr"));
    QVERIFY(style);
    QCOMPARE(style->elements.size(), 14);
    QVERIFY(!style->elements.contains(QStringLiteral("filterBar.right")));
    QVERIFY(!style->elements.contains(QStringLiteral("view.right")));
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
                          WhaleStyleManager::saveLastActionName(),
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

void WhaleStyleManagerTest::testSaveLastStyle()
{
    DolphinMainWindow *window = openWindow();
    QVERIFY(window);
    WhaleStyleManager *manager = WhaleStyleManager::ensure(window);
    QAction *saveLast = window->actionCollection()->action(WhaleStyleManager::saveLastActionName());
    QVERIFY(saveLast);
    // Nothing applied yet.
    QVERIFY(!saveLast->isEnabled());
    QVERIFY(!manager->saveLastStyle());

    // Saving as a style makes it the last one.
    DolphinView *view = tabPage(window)->primaryViewContainer()->view();
    view->setViewMode(DolphinView::IconsView);
    const QString bilder = manager->saveCurrentAsStyle(QStringLiteral("Bilder"));
    QCOMPARE(manager->lastStyleId(), bilder);
    QVERIFY(saveLast->isEnabled());
    QVERIFY(saveLast->text().contains(QLatin1String("Bilder")));
    WhaleStyle style = *WhaleStyles::self()->store().style(bilder);
    style.shortcut = QKeySequence(QStringLiteral("Ctrl+Alt+2"));
    style.elements[QStringLiteral("panels")].enabled = false;
    WhaleStyles::self()->editableStore().updateStyle(style);
    WhaleStyles::self()->save();

    view->setViewMode(DolphinView::CompactView);
    const QString schlicht = manager->saveCurrentAsStyle(QStringLiteral("Schlicht"));
    QVERIFY(saveLast->text().contains(QLatin1String("Schlicht")));

    // Applying makes it the last one; "Save Profile" captures it again.
    window->actionCollection()->action(WhaleStyleManager::actionName(bilder))->trigger();
    QCOMPARE(view->viewMode(), DolphinView::IconsView);
    QCOMPARE(manager->lastStyleId(), bilder);
    view->setViewMode(DolphinView::DetailsView);
    saveLast->trigger();
    const WhaleStyle *saved = WhaleStyles::self()->store().style(bilder);
    QVERIFY(saved->elements.value(QStringLiteral("viewStyle.left")).value.contains(QLatin1String("ViewMode=1")));
    QCOMPARE(saved->shortcut, QKeySequence(QStringLiteral("Ctrl+Alt+2")));
    QVERIFY(!saved->elements.value(QStringLiteral("panels")).enabled);
    QCOMPARE(WhaleStyles::self()->store().ids(), (QStringList{bilder, schlicht}));
    // Saved at once.
    QVERIFY(WhaleStyleStore(WhaleStyleStore::defaultConfig())
                .style(bilder)
                ->elements.value(QStringLiteral("viewStyle.left"))
                .value.contains(QLatin1String("ViewMode=1")));

    // Renamed: the entry follows.
    WhaleStyles::self()->editableStore().renameStyle(bilder, QStringLiteral("Fotos"));
    WhaleStyles::self()->save();
    QVERIFY(saveLast->text().contains(QLatin1String("Fotos")));

    // A new window starts with the style remembered last.
    DolphinMainWindow *second = openWindow();
    QVERIFY(second);
    QCOMPARE(WhaleStyleManager::ensure(second)->lastStyleId(), bilder);

    // Deleted: nothing to save.
    WhaleStyles::self()->editableStore().removeStyle(bilder);
    WhaleStyles::self()->save();
    QVERIFY(!saveLast->isEnabled());
    QVERIFY(!manager->saveLastStyle());
    QCOMPARE(WhaleStyles::self()->store().ids(), QStringList{schlicht});
}

void WhaleStyleManagerTest::testStyleWithoutShownViews()
{
    // A style saved before view.left and view.right existed shows the file list, not the view
    // the folder prefers: Whale started with the treemap and such styles could not leave it.
    const QString treemap = QStringLiteral("orcinus_treemap");
    WhaleViewPreferences::instance()->setPreferredView(m_dir->url(), treemap);
    DolphinMainWindow *window = openWindow();
    QVERIFY(window);
    WhaleViewHost *left = tabPage(window)->primaryViewContainer()->viewHost();
    QTRY_COMPARE(left->currentViewId(), treemap);

    WhaleStyle old;
    old.name = QStringLiteral("Old");
    old.elements.insert(QStringLiteral("split"), {true, QStringLiteral("false")});
    const QString oldId = WhaleStyles::self()->editableStore().addStyle(old);
    WhaleStyles::self()->save();
    WhaleStyleManager *manager = WhaleStyleManager::ensure(window);
    QVERIFY(manager->applyStyle(oldId));
    QCOMPARE(left->currentViewId(), WhaleViewHost::dolphinViewId());
    // Applying a style still writes no preferred view.
    QCOMPARE(WhaleViewPreferences::instance()->preferredView(m_dir->url()), treemap);

    // With two views: the new right view first shows the preferred treemap, then the file list.
    WhaleStyle oldSplit;
    oldSplit.name = QStringLiteral("Old Split");
    oldSplit.elements.insert(QStringLiteral("split"), {true, QStringLiteral("true")});
    const QString oldSplitId = WhaleStyles::self()->editableStore().addStyle(oldSplit);
    WhaleStyles::self()->save();
    QVERIFY(manager->applyStyle(oldSplitId));
    QVERIFY(tabPage(window)->splitViewEnabled());
    QCOMPARE(tabPage(window)->secondaryViewContainer()->viewHost()->currentViewId(), WhaleViewHost::dolphinViewId());
    QCOMPARE(left->currentViewId(), WhaleViewHost::dolphinViewId());

    // A view the user unchecked on the settings page stays as it is.
    WhaleStyleElements::self().element(QStringLiteral("view.left"))->apply(window, treemap);
    QCOMPARE(left->currentViewId(), treemap);
    old.id = oldId;
    old.elements.insert(QStringLiteral("view.left"), {false, WhaleViewHost::dolphinViewId()});
    WhaleStyles::self()->editableStore().updateStyle(old);
    WhaleStyles::self()->save();
    QVERIFY(manager->applyStyle(oldId));
    QVERIFY(!tabPage(window)->splitViewEnabled());
    QCOMPARE(left->currentViewId(), treemap);
}

QTEST_MAIN(WhaleStyleManagerTest)

void WhaleStyleManagerTest::testDefaultStyles()
{
    // The shipped profiles: fixed IDs, only known elements, each one applies.
    const QList<WhaleStyle> styles = WhaleDefaultStyles::styles();
    QStringList ids;
    for (const WhaleStyle &style : styles) {
        ids.append(style.id);
        QVERIFY(!style.name.isEmpty());
        for (auto it = style.elements.cbegin(); it != style.elements.cend(); ++it) {
            QVERIFY2(WhaleStyleElements::self().element(it.key()), qPrintable(style.id + QLatin1Char(' ') + it.key()));
            QVERIFY(it.value().enabled);
        }
    }
    QCOMPARE(ids,
             QStringList({QStringLiteral("compact"),
                          QStringLiteral("dual-compact"),
                          QStringLiteral("details"),
                          QStringLiteral("dual-details"),
                          QStringLiteral("images-only"),
                          QStringLiteral("locked-cushion-map")}));
    QVERIFY(ids.contains(WhaleDefaultStyles::startStyleId()));

    // Put only into a store without profiles, and kept there with their IDs.
    WhaleStyleStore &store = WhaleStyles::self()->editableStore();
    QVERIFY(WhaleDefaultStyles::seed(store));
    QCOMPARE(store.ids(), ids);
    QVERIFY(!WhaleDefaultStyles::seed(store));
    WhaleStyles::self()->save();
    WhaleStyles::self()->reload();
    QCOMPARE(WhaleStyles::self()->store().styles(), styles);

    // Saved like in the application: elements that save the settings would otherwise announce
    // init()'s unsaved start-up split as a change, and the window would follow it.
    GeneralSettings::self()->save();
    DolphinMainWindow *window = openWindow();
    QVERIFY(window);
    WhaleStyleManager *manager = WhaleStyleManager::ensure(window);
    QVERIFY(manager->applyStyle(QStringLiteral("dual-details")));
    QTRY_VERIFY(tabPage(window)->splitViewEnabled() && tabPage(window)->secondaryViewContainer());
    QCOMPARE(tabPage(window)->primaryViewContainer()->view()->viewMode(), DolphinView::DetailsView);
    QCOMPARE(tabPage(window)->secondaryViewContainer()->view()->viewMode(), DolphinView::DetailsView);

    QVERIFY(manager->applyStyle(QStringLiteral("images-only")));
    QTRY_VERIFY(!tabPage(window)->splitViewEnabled());
    DolphinViewContainer *container = tabPage(window)->primaryViewContainer();
    QCOMPARE(container->view()->viewMode(), DolphinView::IconsView);
    QTRY_VERIFY(container->isFilterBarVisible());
    QCOMPARE(container->view()->nameFilter(), QStringLiteral("*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff *.svg *.ico *.avif *.heic *.heif"));
    QCOMPARE(container->view()->itemsCount(), 0); // a.txt and b.txt
    QVERIFY(dock(window, "placesDock")->isHidden());

    QVERIFY(manager->applyStyle(WhaleDefaultStyles::startStyleId()));
    container = tabPage(window)->primaryViewContainer();
    QCOMPARE(container->view()->viewMode(), DolphinView::CompactView);
    QTRY_VERIFY(!container->isFilterBarVisible());
    QVERIFY(!dock(window, "placesDock")->isHidden());
    QCOMPARE(manager->lastStyleId(), WhaleDefaultStyles::startStyleId());
}

#include "whalestylemanagertest.moc"
