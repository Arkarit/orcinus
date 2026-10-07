/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
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
#include "views/dolphinviewactionhandler.h"
#include "whalecontentscope.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewhost.h"
#include "whaleviewpreferences.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KLocalizedString>

#include <QMenu>
#include <QStandardPaths>
#include <QTest>

namespace
{
const QString treemapId = QStringLiteral("whale_treemap");
const QString fileListModes[] = {QStringLiteral("icons"), QStringLiteral("compact"), QStringLiteral("details")};

QVariantMap treemapState(QWidget *view)
{
    QVariantMap state;
    QMetaObject::invokeMethod(view, "treemapState", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, state));
    return state;
}

/// The entries of @p menu as the user sees them: "-" for a separator, "name >" for a submenu.
QStringList entryTexts(const QMenu *menu)
{
    QStringList texts;
    const auto actions = menu->actions();
    for (const QAction *action : actions) {
        if (!action->isVisible()) {
            continue;
        }
        const QString text = KLocalizedString::removeAcceleratorMarker(action->text());
        texts.append(action->isSeparator() ? QStringLiteral("-") : action->menu() ? text + QStringLiteral(" >") : text);
    }
    return texts;
}
}

/**
 * The View menus: one flat list of view modes (the file list's modes and the plugin views),
 * a common "View Modes" submenu for more than five of them, and the view options "Show flat
 * (no folders)", "Show Previews" and "Show Hidden Files", enabled by what the shown view
 * supports while their settings are kept.
 */
class WhaleViewMenuTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testDirectViewModes();
    void testViewModeCount_data();
    void testViewModeCount();
    void testSubmenuHoldsAllViewModes();
    void testExactlyOneViewModeChecked();
    void testShownViewEntryStaysChecked();
    void testOptionsInFileList();
    void testOptionsInTreemap();
    void testOptionSettingsAreKept();
    void testFlatEnabledAfterTreemap();
    void testLocations();
    void testHistoryAndSplitView();

private:
    /// A main window that finds the view plugins in @p pluginDirs only.
    void createMainWindow(const QStringList &pluginDirs);
    DolphinViewContainer *showMainWindow(const QUrl &url);
    WhaleViewActions *viewActions() const;
    QAction *action(const QString &name) const;
    QStringList viewModeIds() const;
    QStringList checkedViewModes() const;
    /// Triggers the entry of the view mode @p id, as the user does.
    bool choose(const QString &id);
    QMenu *viewSettingsMenu() const;
    QMenu *menuBarViewMenu() const;
    QUrl url(const QString &name) const;

    QTemporaryDir m_tmpHome;
    QStringList m_libraryPaths;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
};

void WhaleViewMenuTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    KLocalizedString::setApplicationDomain({"whale"});
    m_libraryPaths = QCoreApplication::libraryPaths();
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleViewMenuTest::init()
{
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/whaleviewpreferencesrc"));
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a/big.bin"), QByteArray(40000, 'a'));
    m_dir->createFile(QStringLiteral("a/sub/small.txt"), QByteArray(3000, 's'));
    m_dir->createFile(QStringLiteral("a/sub/deeper/tiny.txt"), QByteArray(1000, 't'));
    m_dir->createFile(QStringLiteral("a/.hidden.bin"), QByteArray(20000, 'h'));
    m_dir->createFile(QStringLiteral("b/one.txt"), QByteArray(2000, 'b'));
    createMainWindow({QStringLiteral(WHALE_TEST_PLUGIN_DIR)});
}

void WhaleViewMenuTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
}

void WhaleViewMenuTest::createMainWindow(const QStringList &pluginDirs)
{
    m_mainWindow.reset();
    // The registry of a window searches the plugin directories once, when it is created.
    // The build tree's plugins come first, before installed ones with the same ID.
    QCoreApplication::setLibraryPaths(pluginDirs + m_libraryPaths);
    m_mainWindow.reset(new DolphinMainWindow());
}

DolphinViewContainer *WhaleViewMenuTest::showMainWindow(const QUrl &url)
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
    container->view()->setHiddenFilesShown(false);
    container->view()->setPreviewsShown(false);
    return container;
}

WhaleViewActions *WhaleViewMenuTest::viewActions() const
{
    return m_mainWindow->findChild<WhaleViewActions *>();
}

QAction *WhaleViewMenuTest::action(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

QStringList WhaleViewMenuTest::viewModeIds() const
{
    QStringList ids;
    const auto actions = viewActions()->viewModeActions();
    for (const QAction *entry : actions) {
        ids.append(viewActions()->viewModeId(entry));
    }
    return ids;
}

QStringList WhaleViewMenuTest::checkedViewModes() const
{
    QStringList ids;
    const auto actions = viewActions()->viewModeActions();
    for (const QAction *entry : actions) {
        if (entry->isChecked()) {
            ids.append(viewActions()->viewModeId(entry));
        }
    }
    return ids;
}

bool WhaleViewMenuTest::choose(const QString &id)
{
    QAction *entry = viewActions()->viewModeAction(id);
    if (!entry || !entry->isEnabled()) {
        return false;
    }
    entry->trigger();
    return true;
}

QMenu *WhaleViewMenuTest::viewSettingsMenu() const
{
    QAction *viewSettings = action(QStringLiteral("view_settings"));
    return viewSettings ? viewSettings->menu() : nullptr;
}

QMenu *WhaleViewMenuTest::menuBarViewMenu() const
{
    return m_mainWindow->findChild<QMenu *>(QStringLiteral("view"));
}

QUrl WhaleViewMenuTest::url(const QString &name) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + name);
}

void WhaleViewMenuTest::testDirectViewModes()
{
    QVERIFY(showMainWindow(url(QStringLiteral("a"))));

    // The product's views: the file list's modes, then the treemap. No test views.
    QCOMPARE(viewModeIds(), QStringList({QStringLiteral("icons"), QStringLiteral("compact"), QStringLiteral("details"), treemapId}));
    QCOMPARE(viewActions()->viewModeMenuEntries(), viewActions()->viewModeActions());
    QVERIFY(!action(QStringLiteral("whale_view_switcher")));

    // The toolbar's view settings dropdown: one level of view modes, then the order and
    // additional information, then the view options.
    QMenu *menu = viewSettingsMenu();
    QVERIFY(menu);
    const QStringList texts = entryTexts(menu);
    QCOMPARE(texts.mid(0, 5),
             QStringList({QStringLiteral("Icons"), QStringLiteral("Compact"), QStringLiteral("Details"), QStringLiteral("Treemap"), QStringLiteral("-")}));
    QVERIFY2(!texts.contains(QStringLiteral("View Type >")), qPrintable(texts.join(QLatin1Char('|'))));
    QVERIFY2(!texts.contains(QStringLiteral("File List")), qPrintable(texts.join(QLatin1Char('|'))));
    QVERIFY2(!texts.contains(QStringLiteral("Hello")) && !texts.contains(QStringLiteral("Test List")), qPrintable(texts.join(QLatin1Char('|'))));
    const auto entries = menu->actions();
    const int flat = entries.indexOf(action(QStringLiteral("whale_include_subfolders")));
    QVERIFY(flat > 0);
    QVERIFY(entries.at(flat - 1)->isSeparator());
    QCOMPARE(entries.indexOf(action(QStringLiteral("show_preview"))), flat + 1);
    QCOMPARE(entries.indexOf(action(QStringLiteral("show_hidden_files"))), flat + 2);
    QCOMPARE(action(QStringLiteral("whale_include_subfolders"))->text(), QStringLiteral("Show flat (no folders)"));
    // Zoom, Sort By, Group By and Show Additional Information form the group in between.
    const int secondGroup = texts.indexOf(QStringLiteral("-")) + 1;
    QCOMPARE(entries.at(secondGroup), action(QStringLiteral("zoom")));
    QCOMPARE(entries.at(flat - 2), action(QStringLiteral("additional_info")));
    QVERIFY(entries.mid(secondGroup, flat - 1 - secondGroup).contains(action(QStringLiteral("sort"))));
    QVERIFY(entries.mid(secondGroup, flat - 1 - secondGroup).contains(action(QStringLiteral("group_by"))));

    // The menu bar's View menu has the same view modes as a group after KDE's standard zoom
    // actions (whose place in the View menu is given by KDE's standard menus).
    QMenu *viewMenu = menuBarViewMenu();
    QVERIFY(viewMenu);
    const QStringList menuTexts = entryTexts(viewMenu);
    const int menuModes = menuTexts.indexOf(QStringLiteral("Icons"));
    QVERIFY2(menuModes > 0, qPrintable(menuTexts.join(QLatin1Char('|'))));
    QCOMPARE(menuTexts.at(menuModes - 1), QStringLiteral("-"));
    QCOMPARE(menuTexts.mid(menuModes, 6), texts.mid(0, 4) + QStringList({QStringLiteral("-"), QStringLiteral("Sort By >")}));
    QVERIFY(menuTexts.indexOf(QStringLiteral("Zoom Out")) < menuModes);
    QVERIFY2(!menuTexts.contains(QStringLiteral("View Type >")), qPrintable(menuTexts.join(QLatin1Char('|'))));
    QVERIFY2(!menuTexts.contains(QStringLiteral("Change View Mode >")), qPrintable(menuTexts.join(QLatin1Char('|'))));
    const auto menuEntries = viewMenu->actions();
    const int menuFlat = menuEntries.indexOf(action(QStringLiteral("whale_include_subfolders")));
    QVERIFY(menuFlat > 0 && menuEntries.at(menuFlat - 1)->isSeparator());
    QCOMPARE(menuEntries.indexOf(action(QStringLiteral("show_preview"))), menuFlat + 1);
    QCOMPARE(menuEntries.indexOf(action(QStringLiteral("show_hidden_files"))), menuFlat + 2);

    // Again after the GUI was rebuilt (e.g. by configuring the toolbars), once.
    QVERIFY(QMetaObject::invokeMethod(m_mainWindow.data(), "saveNewToolbarConfig"));
    QCOMPARE(entryTexts(menuBarViewMenu()), menuTexts);
    QCOMPARE(entryTexts(menuBarViewMenu()).count(QStringLiteral("Treemap")), 1);

    // Dolphin's "Change View Mode" (context menu, hamburger menu) holds the same list.
    QCOMPARE(action(QStringLiteral("view_mode"))->menu()->actions(), viewActions()->viewModeActions());
}

void WhaleViewMenuTest::testViewModeCount_data()
{
    QTest::addColumn<QStringList>("pluginDirs");
    QTest::addColumn<int>("count");
    QTest::addColumn<bool>("submenu");

    const QString product = QStringLiteral(WHALE_TEST_PLUGIN_DIR);
    const QString dynamic = QStringLiteral(WHALE_TEST_DYNAMIC_PLUGIN_DIR);
    const QString testViews = QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR);
    QTest::newRow("product: 4") << QStringList{product} << 4 << false;
    QTest::newRow("5") << QStringList{product, dynamic} << 5 << false;
    QTest::newRow("6") << QStringList{product, testViews} << 6 << true;
    QTest::newRow("7") << QStringList{product, testViews, dynamic} << 7 << true;
}

void WhaleViewMenuTest::testViewModeCount()
{
    QFETCH(QStringList, pluginDirs);
    QFETCH(int, count);
    QFETCH(bool, submenu);

    createMainWindow(pluginDirs);
    QVERIFY(showMainWindow(url(QStringLiteral("a"))));
    QCOMPARE(viewActions()->viewModeActions().count(), count);
    QCOMPARE(viewModeIds().mid(0, 3), QStringList(std::begin(fileListModes), std::end(fileListModes)));

    const QStringList texts = entryTexts(viewSettingsMenu());
    const QStringList menuTexts = entryTexts(menuBarViewMenu());
    const int sortBy = menuTexts.indexOf(QStringLiteral("Sort By >"));
    if (submenu) {
        QCOMPARE(viewActions()->viewModeMenuEntries().count(), 1);
        QCOMPARE(texts.mid(0, 2), QStringList({QStringLiteral("View Modes >"), QStringLiteral("-")}));
        QCOMPARE(menuTexts.mid(sortBy - 3, 3), QStringList({QStringLiteral("-"), QStringLiteral("View Modes >"), QStringLiteral("-")}));
        QVERIFY(!texts.contains(QStringLiteral("Icons")));
        QVERIFY(!menuTexts.contains(QStringLiteral("Icons")));
    } else {
        QCOMPARE(viewActions()->viewModeMenuEntries(), viewActions()->viewModeActions());
        QCOMPARE(texts.indexOf(QStringLiteral("-")), count);
        QCOMPARE(menuTexts.mid(sortBy - count - 2, count + 2).constFirst(), QStringLiteral("-"));
        QCOMPARE(menuTexts.mid(sortBy - count - 1, count), texts.mid(0, count));
        QVERIFY(!texts.contains(QStringLiteral("View Modes >")));
    }
}

void WhaleViewMenuTest::testSubmenuHoldsAllViewModes()
{
    createMainWindow({QStringLiteral(WHALE_TEST_PLUGIN_DIR), QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR)});
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);

    // One submenu with all view modes, the file list's and the plugin views' alike.
    QAction *viewModes = viewActions()->viewModeMenuEntries().constFirst();
    QVERIFY(viewModes->menu());
    QCOMPARE(viewModes->text(), QStringLiteral("View Modes"));
    QCOMPARE(viewModes->menu()->actions(), viewActions()->viewModeActions());
    QCOMPARE(entryTexts(viewModes->menu()),
             QStringList({QStringLiteral("Icons"),
                          QStringLiteral("Compact"),
                          QStringLiteral("Details"),
                          QStringLiteral("Hello"),
                          QStringLiteral("Test List"),
                          QStringLiteral("Treemap")}));
    QCOMPARE(viewSettingsMenu()->actions().first(), viewModes);
    for (QAction *entry : viewActions()->viewModeActions()) {
        QVERIFY(!viewSettingsMenu()->actions().contains(entry));
    }

    // Choosing from it works for both kinds.
    QVERIFY(choose(QStringLiteral("whale_testlist")));
    QCOMPARE(container->viewHost()->currentViewId(), QStringLiteral("whale_testlist"));
    QCOMPARE(checkedViewModes(), QStringList{QStringLiteral("whale_testlist")});
    QVERIFY(choose(QStringLiteral("compact")));
    QVERIFY(container->viewHost()->isDolphinViewShown());
    QCOMPARE(container->view()->viewMode(), DolphinView::CompactView);
    QCOMPARE(checkedViewModes(), QStringList{QStringLiteral("compact")});
}

void WhaleViewMenuTest::testExactlyOneViewModeChecked()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QCOMPARE(checkedViewModes().count(), 1);

    const QList<std::pair<QString, DolphinView::Mode>> modes = {
        {QStringLiteral("icons"), DolphinView::IconsView},
        {QStringLiteral("compact"), DolphinView::CompactView},
        {QStringLiteral("details"), DolphinView::DetailsView},
    };
    for (int round = 0; round < 2; ++round) {
        for (const auto &[id, mode] : modes) {
            QVERIFY2(choose(id), qPrintable(id));
            QVERIFY(host->isDolphinViewShown());
            QCOMPARE(container->view()->viewMode(), mode);
            QCOMPARE(checkedViewModes(), QStringList{id});

            // From each mode to the treemap and back to the mode.
            QVERIFY(choose(treemapId));
            QCOMPARE(host->currentViewId(), treemapId);
            QCOMPARE(checkedViewModes(), QStringList{treemapId});
            QCOMPARE(container->view()->viewMode(), mode); // the file list keeps its mode
            QVERIFY(choose(id));
            QVERIFY(host->isDolphinViewShown());
            QCOMPARE(checkedViewModes(), QStringList{id});
        }
    }
}

void WhaleViewMenuTest::testShownViewEntryStaysChecked()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    QVERIFY(choose(QStringLiteral("details")));
    QVERIFY(choose(treemapId));

    // Dolphin updates its view actions often (e.g. on activating a view); it keeps the
    // treemap checked instead of the file list's mode.
    auto handler = m_mainWindow->findChild<DolphinViewActionHandler *>();
    QVERIFY(handler);
    handler->updateViewActions();
    QCOMPARE(checkedViewModes(), QStringList{treemapId});
    Q_EMIT container->view()->modeChanged(DolphinView::DetailsView, DolphinView::DetailsView);
    QCOMPARE(checkedViewModes(), QStringList{treemapId});
}

void WhaleViewMenuTest::testOptionsInFileList()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    for (const QString &id : fileListModes) {
        QVERIFY(choose(id));
        for (const QString &name : {QStringLiteral("whale_include_subfolders"), QStringLiteral("show_preview"), QStringLiteral("show_hidden_files")}) {
            QVERIFY2(action(name)->isVisible() && action(name)->isEnabled(), qPrintable(id + QLatin1Char(' ') + name));
            QVERIFY2(viewActions()->gateReason(name).isEmpty(), qPrintable(name));
        }
    }
    QVERIFY(container->viewHost()->supportsViewOption(WhaleViewHost::ViewOption::FlatContent));
    QVERIFY(container->viewHost()->supportsViewOption(WhaleViewHost::ViewOption::Previews));
    QVERIFY(container->viewHost()->supportsViewOption(WhaleViewHost::ViewOption::HiddenFiles));
}

void WhaleViewMenuTest::testOptionsInTreemap()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    QVERIFY(choose(treemapId));
    QWidget *view = host->currentView()->widget();
    QTRY_COMPARE(treemapState(view).value(QStringLiteral("status")).toString(), QStringLiteral("ready"));

    // Disabled, but still in the menus.
    for (const QString &name : {QStringLiteral("whale_include_subfolders"), QStringLiteral("show_preview"), QStringLiteral("show_hidden_files")}) {
        QVERIFY2(action(name)->isVisible() && !action(name)->isEnabled(), qPrintable(name));
        QVERIFY(viewSettingsMenu()->actions().contains(action(name)));
    }
    QVERIFY(!host->supportsViewOption(WhaleViewHost::ViewOption::FlatContent));
    QVERIFY(!host->supportsViewOption(WhaleViewHost::ViewOption::Previews));
    QCOMPARE(viewActions()->gateReason(QStringLiteral("show_preview")), QStringLiteral("no Previews capability"));

    // "Show Hidden Files" would have no effect: the treemap counts the hidden entries while
    // the setting is off. So it is disabled (no HiddenFiles capability).
    QVERIFY(!container->view()->hiddenFilesShown());
    const qint64 visibleSize = 40000 + 3000 + 1000;
    QCOMPARE(treemapState(view).value(QStringLiteral("totalSize")).toLongLong(), visibleSize + 20000);
    QVERIFY(!(host->currentView()->capabilities() & WhaleViewCapability::HiddenFiles));
    QVERIFY(!host->supportsViewOption(WhaleViewHost::ViewOption::HiddenFiles));
    QCOMPARE(viewActions()->gateReason(QStringLiteral("show_hidden_files")), QStringLiteral("no HiddenFiles capability"));
}

void WhaleViewMenuTest::testOptionSettingsAreKept()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    DolphinView *dolphinView = container->view();
    QVERIFY(choose(QStringLiteral("details")));

    // Details with previews, hidden files and the files below the location.
    action(QStringLiteral("show_preview"))->trigger();
    action(QStringLiteral("show_hidden_files"))->trigger();
    action(QStringLiteral("whale_include_subfolders"))->trigger();
    QVERIFY(dolphinView->previewsShown());
    QVERIFY(dolphinView->hiddenFilesShown());
    QTRY_VERIFY(host->isRecursiveContentShown());
    QCOMPARE(WhaleViewPreferences::instance()->contentScope(url(QStringLiteral("a"))), WhaleContentScope::RecursiveFiles);

    // The treemap does not use them: disabled, but nothing is switched off or overwritten.
    QVERIFY(choose(treemapId));
    QCOMPARE(WhaleViewPreferences::instance()->preferredView(url(QStringLiteral("a"))), treemapId); // O3
    for (const QString &name : {QStringLiteral("whale_include_subfolders"), QStringLiteral("show_preview"), QStringLiteral("show_hidden_files")}) {
        QVERIFY2(!action(name)->isEnabled(), qPrintable(name));
        QVERIFY2(action(name)->isChecked(), qPrintable(name));
    }
    QVERIFY(dolphinView->previewsShown());
    QVERIFY(dolphinView->hiddenFilesShown());
    QVERIFY(!host->isRecursiveContentShown()); // the treemap sees the location's own items
    QCOMPARE(host->contentScope(), WhaleContentScope::RecursiveFiles); // F1: kept for the file list
    QCOMPARE(WhaleViewPreferences::instance()->contentScope(url(QStringLiteral("a"))), WhaleContentScope::RecursiveFiles);

    // Back to Details: everything as before.
    QVERIFY(choose(QStringLiteral("details")));
    QCOMPARE(WhaleViewPreferences::instance()->preferredView(url(QStringLiteral("a"))), WhaleViewHost::dolphinViewId()); // O3
    QCOMPARE(dolphinView->viewMode(), DolphinView::DetailsView);
    for (const QString &name : {QStringLiteral("whale_include_subfolders"), QStringLiteral("show_preview"), QStringLiteral("show_hidden_files")}) {
        QVERIFY2(action(name)->isEnabled(), qPrintable(name));
        QVERIFY2(action(name)->isChecked(), qPrintable(name));
    }
    QVERIFY(dolphinView->previewsShown());
    QVERIFY(dolphinView->hiddenFilesShown());
    QTRY_VERIFY(host->isRecursiveContentShown());
    QCOMPARE(host->contentScopeOrigin(), QStringLiteral("user"));
}

void WhaleViewMenuTest::testFlatEnabledAfterTreemap()
{
    // With the location's own items (no scope change on the way back), "Show flat (no
    // folders)" is enabled again once the file list is shown again.
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    QAction *flat = action(QStringLiteral("whale_include_subfolders"));
    QVERIFY(flat->isEnabled());
    QVERIFY(!flat->isChecked());
    QVERIFY(choose(treemapId));
    QVERIFY(!flat->isEnabled());
    QVERIFY(choose(QStringLiteral("details")));
    QVERIFY(container->viewHost()->isDolphinViewShown());
    QVERIFY(flat->isEnabled());
    QVERIFY(!flat->isChecked());

    // Also when the file list comes back without the user (history).
    QVERIFY(choose(treemapId));
    container->setUrl(url(QStringLiteral("b")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("b")));
    QVERIFY(choose(QStringLiteral("icons")));
    action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->viewHost()->currentViewId(), treemapId);
    QVERIFY(!flat->isEnabled());
    action(KStandardAction::name(KStandardAction::Forward))->trigger();
    QTRY_VERIFY(container->viewHost()->isDolphinViewShown());
    QTRY_COMPARE(container->url(), url(QStringLiteral("b")));
    QVERIFY(flat->isEnabled());

    // Toggling it works again.
    flat->trigger();
    QTRY_VERIFY(container->viewHost()->isRecursiveContentShown());
}

void WhaleViewMenuTest::testLocations()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    QAction *treemapEntry = viewActions()->viewModeAction(treemapId);
    QVERIFY(treemapEntry->isEnabled());
    const QList<QAction *> modes = viewActions()->viewModeActions();

    // trash:/ is no local folder: the treemap's entry is disabled but stays, and the file list
    // offers no files below the location.
    container->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_COMPARE(container->url(), QUrl(QStringLiteral("trash:/")));
    QVERIFY(container->viewHost()->isDolphinViewShown());
    QVERIFY(!treemapEntry->isEnabled());
    QCOMPARE(viewActions()->viewModeActions(), modes);
    QCOMPARE(entryTexts(viewSettingsMenu()).mid(0, 4),
             QStringList({QStringLiteral("Icons"), QStringLiteral("Compact"), QStringLiteral("Details"), QStringLiteral("Treemap")}));
    QCOMPARE(checkedViewModes().count(), 1);
    QVERIFY(!action(QStringLiteral("whale_include_subfolders"))->isEnabled());
    QVERIFY(action(QStringLiteral("show_preview"))->isEnabled());
    QVERIFY(action(QStringLiteral("show_hidden_files"))->isEnabled());

    // Back at a local folder.
    container->setUrl(url(QStringLiteral("b")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("b")));
    QVERIFY(treemapEntry->isEnabled());
    QVERIFY(action(QStringLiteral("whale_include_subfolders"))->isEnabled());
}

void WhaleViewMenuTest::testHistoryAndSplitView()
{
    DolphinViewContainer *container = showMainWindow(url(QStringLiteral("a")));
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();

    // History: the treemap at A, the file list at B; back to A shows (and checks) the treemap.
    QVERIFY(choose(treemapId));
    container->setUrl(url(QStringLiteral("b")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("b")));
    QVERIFY(choose(QStringLiteral("icons")));
    QVERIFY(host->isDolphinViewShown());
    QCOMPARE(checkedViewModes(), QStringList{QStringLiteral("icons")});
    action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("a")));
    QTRY_COMPARE(host->currentViewId(), treemapId);
    QCOMPARE(checkedViewModes(), QStringList{treemapId});

    // Split view: the entries follow the active side.
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>()->currentTabPage();
    QTRY_VERIFY(page->secondaryViewContainer());
    DolphinViewContainer *right = page->secondaryViewContainer();
    right->setActive(true);
    QTRY_VERIFY(right->isActive());
    if (!right->viewHost()->isDolphinViewShown()) {
        QVERIFY(choose(QStringLiteral("compact")));
    }
    QVERIFY(right->viewHost()->isDolphinViewShown());
    QCOMPARE(checkedViewModes().count(), 1);
    QVERIFY(checkedViewModes().constFirst() != treemapId);
    QVERIFY(action(QStringLiteral("show_preview"))->isEnabled());
    container->setActive(true);
    QTRY_VERIFY(container->isActive());
    QCOMPARE(checkedViewModes(), QStringList{treemapId});
    QVERIFY(!action(QStringLiteral("show_preview"))->isEnabled());
}

QTEST_MAIN(WhaleViewMenuTest)

#include "whaleviewmenutest.moc"
