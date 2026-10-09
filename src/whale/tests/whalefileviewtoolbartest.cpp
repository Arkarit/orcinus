/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalefileviewtoolbar.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalefileviewtoolbarclient.h"
#include "whaletoolbarmanager.h"
#include "whaleviewactions.h"
#include "whaleviewcommandscope.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"
#include "whaleviewservices.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KStandardAction>
#include <KToolBarPopupAction>

#include <QAccessible>
#include <QDialog>
#include <QDockWidget>
#include <QDomDocument>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QToolButton>

namespace
{
const QString treemapId = QStringLiteral("whale_treemap");
const QStringList defaultLayout = {QStringLiteral("go_back"),
                                   QStringLiteral("go_forward"),
                                   QStringLiteral("go_up"),
                                   QStringLiteral("view_redisplay"),
                                   QStringLiteral("|"),
                                   QStringLiteral("view_settings")};

// All actions an instance shows: the collection and, recursively, the entries of menus.
QList<QAction *> reachableActions(const QList<QAction *> &actions)
{
    QList<QAction *> result;
    for (QAction *action : actions) {
        if (!action || result.contains(action)) {
            continue;
        }
        result.append(action);
        if (action->menu()) {
            result += reachableActions(action->menu()->actions());
        }
    }
    return result;
}
}

/**
 * The File View Toolbar (multi-toolbar plan, TB2): one toolbar per file view, acting on that
 * view only.
 */
class WhaleFileViewToolBarTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testLayoutFromDocument();
    void testDefaultLayout();
    void testUserLayout();
    void testOneToolBarPerView();
    void testSplitLifecycle();
    void testTabs();
    void testActionIdentityAndShortcuts();
    void testTargeting();
    void testIndependentNavigationState();
    void testViewSettingsPerView();
    void testViewSettingsMenuIsPopup();
    void testNoAmbiguousShortcut();
    void testThirdContainer();
    void testSessionRestore();
    void testClosingWindow();

private:
    DolphinViewContainer *showMainWindow();
    void showSplit();
    DolphinTabPage *tabPage() const;
    static WhaleFileViewToolBar *toolBar(DolphinViewContainer *container);
    static QAction *local(DolphinViewContainer *container, const QString &name);
    static QStringList layoutOf(WhaleFileViewToolBar *toolBar);
    QAction *global(const QString &name) const;
    void clickLocal(DolphinViewContainer *container, const QString &name);
    QString path(const QString &relative) const;
    static QString localLayoutFile();

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    DolphinViewContainer *m_left = nullptr;
    DolphinViewContainer *m_right = nullptr;
};

void WhaleFileViewToolBarTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_tmpHome.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    KLocalizedString::setApplicationDomain({"whale"});
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleFileViewToolBarTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(3000, 'a'));
    m_dir->createFile(QStringLiteral(".hidden"), QByteArray(100, 'h'));
    m_dir->createFile(QStringLiteral("sub/b.txt"), QByteArray(1000, 'b'));
    m_dir->createFile(QStringLiteral("sub2/c.txt"), QByteArray(500, 'c'));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleFileViewToolBarTest::cleanup()
{
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (qobject_cast<QDialog *>(window) && window->isVisible()) {
            window->close();
        }
    }
    m_mainWindow.reset();
    m_dir.reset();
    m_left = m_right = nullptr;
    // A user layout of a test (also of a failed one) must not reach the next test.
    QFile::remove(localLayoutFile());
}

QString WhaleFileViewToolBarTest::localLayoutFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kxmlgui5/") + QStringLiteral(WHALE_COMPONENT)
        + QStringLiteral("/whalefileviewtoolbarui.rc");
}

DolphinViewContainer *WhaleFileViewToolBarTest::showMainWindow()
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1200, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 3;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

void WhaleFileViewToolBarTest::showSplit()
{
    QVERIFY(showMainWindow());
    global(QStringLiteral("split_view"))->trigger();
    m_left = tabPage()->primaryViewContainer();
    m_right = tabPage()->secondaryViewContainer();
    QVERIFY(m_left && m_right);
    QTRY_COMPARE(m_right->view()->itemsCount(), 3);
    QTRY_VERIFY(toolBar(m_left)->scope() && toolBar(m_right)->scope());
}

DolphinTabPage *WhaleFileViewToolBarTest::tabPage() const
{
    return m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
}

WhaleFileViewToolBar *WhaleFileViewToolBarTest::toolBar(DolphinViewContainer *container)
{
    return container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly);
}

QAction *WhaleFileViewToolBarTest::local(DolphinViewContainer *container, const QString &name)
{
    WhaleViewCommandScope *scope = toolBar(container)->scope();
    return scope ? scope->action(name) : nullptr;
}

QStringList WhaleFileViewToolBarTest::layoutOf(WhaleFileViewToolBar *toolBar)
{
    QStringList result;
    const auto actions = toolBar->actions();
    for (QAction *action : actions) {
        if (action->isSeparator()) {
            result.append(QStringLiteral("-"));
        } else if (action->objectName().isEmpty()) {
            result.append(QStringLiteral("|")); // the spacer widget
        } else {
            result.append(action->objectName());
        }
    }
    return result;
}

QAction *WhaleFileViewToolBarTest::global(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

void WhaleFileViewToolBarTest::clickLocal(DolphinViewContainer *container, const QString &name)
{
    auto button = qobject_cast<QToolButton *>(toolBar(container)->widgetForAction(local(container, name)));
    QVERIFY(button);
    QVERIFY(button->isEnabled());
    QTest::mouseClick(button, Qt::LeftButton);
}

QString WhaleFileViewToolBarTest::path(const QString &relative) const
{
    return m_dir->path() + QLatin1Char('/') + relative;
}

void WhaleFileViewToolBarTest::testLayoutFromDocument()
{
    QDomDocument document;
    QVERIFY(
        document.setContent(QStringLiteral("<gui name=\"x\" version=\"1\">"
                                           "<ToolBar name=\"other\"><Action name=\"nope\"/></ToolBar>"
                                           "<ToolBar name=\"whaleFileViewToolBar\"><text>File View Toolbar</text>"
                                           "<Action name=\"go_up\"/><Separator name=\"separator_0\"/><Action name=\"go_back\"/>"
                                           "<Spacer name=\"spacer_0\"/><Action name=\"view_settings\"/><Action/></ToolBar></gui>")));
    const WhaleToolbarLayout layout = WhaleToolbarLayout::fromDocument(document, QStringLiteral("whaleFileViewToolBar"));
    QCOMPARE(layout.toStringList(),
             QStringList({QStringLiteral("go_up"), QStringLiteral("-"), QStringLiteral("go_back"), QStringLiteral("|"), QStringLiteral("view_settings")}));
    QVERIFY(WhaleToolbarLayout::fromDocument(document, QStringLiteral("missing")).entries().isEmpty());
}

void WhaleFileViewToolBarTest::testDefaultLayout()
{
    WhaleFileViewToolbarClient client;
    QCOMPARE(client.layout().toStringList(), defaultLayout);

    // The manager knows both kinds; only the File View Toolbar acts on its own view.
    WhaleToolbarManager *manager = WhaleToolbarManager::ensure(m_mainWindow.data());
    QCOMPARE(manager, WhaleToolbarManager::ensure(m_mainWindow.data()));
    QCOMPARE(manager->parent(), m_mainWindow.data());
    const auto descriptors = manager->descriptors();
    QCOMPARE(descriptors.count(), 2);
    QCOMPARE(descriptors.at(0).id, WhaleToolbarDescriptor::globalId());
    QCOMPARE(descriptors.at(0).xmlGuiToolBar, QStringLiteral("mainToolBar"));
    QCOMPARE(descriptors.at(0).target, WhaleToolbarDescriptor::Target::ActiveView);
    const WhaleToolbarDescriptor fileView = manager->descriptor(WhaleToolbarDescriptor::fileViewId());
    QCOMPARE(fileView.target, WhaleToolbarDescriptor::Target::OwnFileView);
    QCOMPARE(fileView.rcFile, QStringLiteral("whalefileviewtoolbarui.rc"));
    QCOMPARE(manager->fileViewLayout().toStringList(), defaultLayout);
    // basic_actions (Cut/Copy/Paste are missing in a view's own actions) is not offered.
    QVERIFY(!fileView.allowedActions.contains(QStringLiteral("basic_actions")));
}

void WhaleFileViewToolBarTest::testUserLayout()
{
    // The user's copy (same version) wins, as for every XML GUI file.
    const QString file = localLayoutFile();
    QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
    {
        QFile local(file);
        QVERIFY(local.open(QIODevice::WriteOnly));
        local.write(
            "<!DOCTYPE gui SYSTEM \"kpartgui.dtd\">\n<gui name=\"whale_fileview\" version=\"1\">"
            "<ToolBar noMerge=\"1\" name=\"whaleFileViewToolBar\"><text>File View Toolbar</text>"
            "<Action name=\"view_settings\"/><Separator/><Action name=\"go_home\"/><Action name=\"basic_actions\"/></ToolBar></gui>");
    }
    QVERIFY(showMainWindow());
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    QTRY_VERIFY(toolBar(container)->scope());
    // Actions that are not allowed (basic_actions) are left out.
    QCOMPARE(layoutOf(toolBar(container)), QStringList({QStringLiteral("view_settings"), QStringLiteral("-"), QStringLiteral("go_home")}));

    // Back to the default: all toolbars follow the manager.
    QSignalSpy changed(WhaleToolbarManager::of(container), &WhaleToolbarManager::fileViewLayoutChanged);
    QVERIFY(QFile::remove(file));
    WhaleToolbarManager::of(container)->reloadFileViewLayout();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(layoutOf(toolBar(container)), defaultLayout);
}

void WhaleFileViewToolBarTest::testOneToolBarPerView()
{
    QVERIFY(showMainWindow());
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    const auto toolBars = m_mainWindow->findChildren<WhaleFileViewToolBar *>();
    QCOMPARE(toolBars.count(), 1);
    WhaleFileViewToolBar *bar = toolBar(container);
    QVERIFY(bar && bar->isVisible());
    QTRY_VERIFY(bar->scope());
    QCOMPARE(bar->scope()->container(), container);
    QCOMPARE(layoutOf(bar), defaultLayout);
    // The first row of the view: above the view area (and where the search bar would be).
    QVERIFY(bar->geometry().bottom() < container->viewHost()->geometry().top());
    QCOMPARE(bar->geometry().top(), 0);
    // Accessible: each button has a name and the main window as an accessible ancestor (as
    // DolphinMainWindowTest::testAccessibilityTree() requires for focusable widgets).
    QAccessibleInterface *windowInterface = QAccessible::queryAccessibleInterface(m_mainWindow.data());
    const auto barActions = bar->actions();
    for (QAction *action : barActions) {
        QWidget *button = bar->widgetForAction(action);
        if (!qobject_cast<QToolButton *>(button)) {
            continue; // the spacer
        }
        QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(button);
        QVERIFY(interface);
        QVERIFY2(!interface->text(QAccessible::Name).isEmpty(), qPrintable(action->objectName()));
        while (interface && interface != windowInterface) {
            interface = interface->parent();
        }
        QVERIFY2(interface, qPrintable(action->objectName()));
    }
    // Not one of the window's toolbars (Settings › Toolbars Shown, saved window settings).
    QVERIFY(!m_mainWindow->toolBars().contains(bar));
    // One manager and one set of view services for the window.
    QCOMPARE(m_mainWindow->findChildren<WhaleToolbarManager *>().count(), 1);
    QCOMPARE(m_mainWindow->findChildren<WhaleViewRegistry *>().count(), 1);
    QCOMPARE(bar->scope()->viewActions()->services(), WhaleViewServices::of(m_mainWindow.data()));
}

void WhaleFileViewToolBarTest::testSplitLifecycle()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    QCOMPARE(m_mainWindow->findChildren<WhaleFileViewToolBar *>().count(), 2);
    QVERIFY(toolBar(m_left) != toolBar(m_right));

    for (int round = 0; round < 5; ++round) {
        DolphinViewContainer *second = tabPage()->secondaryViewContainer();
        QPointer<WhaleFileViewToolBar> bar = toolBar(second);
        QPointer<WhaleViewCommandScope> scope = bar->scope();
        QPointer<QAction> back = local(second, QStringLiteral("go_back"));
        QVERIFY(bar && scope && back);
        tabPage()->setSplitViewEnabled(false, WithoutAnimation);
        QTRY_VERIFY(!bar);
        QVERIFY(!scope);
        QVERIFY(!back);
        QCOMPARE(m_mainWindow->findChildren<WhaleFileViewToolBar *>().count(), 1);
        tabPage()->setSplitViewEnabled(true, WithoutAnimation);
        QTRY_COMPARE(m_mainWindow->findChildren<WhaleFileViewToolBar *>().count(), 2);
        QTRY_VERIFY(toolBar(tabPage()->secondaryViewContainer())->scope());
    }
    // Still one registry, cache and manager; the window's actions untouched.
    QCOMPARE(m_mainWindow->findChildren<WhaleViewRegistry *>().count(), 1);
    QCOMPARE(m_mainWindow->findChildren<WhaleToolbarManager *>().count(), 1);
    QCOMPARE(m_mainWindow->findChildren<WhaleViewActions *>(QString(), Qt::FindDirectChildrenOnly).count(), 1);
}

void WhaleFileViewToolBarTest::testTabs()
{
    QVERIFY(showMainWindow());
    auto tabWidget = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    DolphinViewContainer *first = m_mainWindow->activeViewContainer();
    for (int round = 0; round < 3; ++round) {
        global(QStringLiteral("new_tab"))->trigger();
        QTRY_COMPARE(tabWidget->count(), 2);
        DolphinViewContainer *second = m_mainWindow->activeViewContainer();
        QVERIFY(second != first);
        QTRY_VERIFY(toolBar(second)->scope());
        QVERIFY(toolBar(second) != toolBar(first));
        QPointer<WhaleFileViewToolBar> bar = toolBar(second);
        tabWidget->closeTab(1);
        QTRY_COMPARE(tabWidget->count(), 1);
        QTRY_VERIFY(!bar);
    }
    QCOMPARE(m_mainWindow->findChildren<WhaleFileViewToolBar *>().count(), 1);
}

void WhaleFileViewToolBarTest::testActionIdentityAndShortcuts()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    QCOMPARE(layoutOf(toolBar(m_left)), layoutOf(toolBar(m_right)));
    const QStringList names = {QStringLiteral("go_back"),
                               QStringLiteral("go_forward"),
                               QStringLiteral("go_up"),
                               QStringLiteral("go_home"),
                               QStringLiteral("view_redisplay"),
                               QStringLiteral("stop"),
                               QStringLiteral("view_settings"),
                               QStringLiteral("show_preview"),
                               QStringLiteral("show_hidden_files"),
                               QStringLiteral("whale_include_subfolders")};
    for (const QString &name : names) {
        QAction *left = local(m_left, name);
        QAction *right = local(m_right, name);
        QVERIFY2(left && right, qPrintable(name));
        QVERIFY(left != right);
        QVERIFY(left != global(name));
        QVERIFY(right != global(name));
    }
    // No shortcuts on any action of a view's own, also in menus; the window's keep theirs.
    for (DolphinViewContainer *container : {m_left, m_right}) {
        const auto actions = reachableActions(toolBar(container)->scope()->actionCollection()->actions() + toolBar(container)->actions());
        QVERIFY(actions.count() > 50);
        for (QAction *action : actions) {
            QVERIFY2(action->shortcuts().isEmpty(), qPrintable(action->objectName() + QLatin1Char(' ') + action->text()));
        }
    }
    QVERIFY(!global(KStandardAction::name(KStandardAction::Back))->shortcuts().isEmpty());
    QVERIFY(!global(QStringLiteral("show_hidden_files"))->shortcuts().isEmpty());
    QVERIFY(!global(KStandardAction::name(KStandardAction::Redisplay))->shortcuts().isEmpty());
}

void WhaleFileViewToolBarTest::testTargeting()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    m_left->setUrl(QUrl::fromLocalFile(path(QStringLiteral("sub"))));
    m_right->setUrl(QUrl::fromLocalFile(path(QStringLiteral("sub2"))));
    QTRY_COMPARE(m_left->url().fileName(), QStringLiteral("sub"));
    QTRY_COMPARE(m_right->url().fileName(), QStringLiteral("sub2"));

    // Back in the right toolbar while the left view is active: the right view goes back, and
    // it is the active view afterwards.
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QTRY_VERIFY(local(m_right, QStringLiteral("go_back"))->isEnabled());
    clickLocal(m_right, QStringLiteral("go_back"));
    QTRY_COMPARE(m_right->url(), m_dir->url());
    QCOMPARE(m_left->url().fileName(), QStringLiteral("sub"));
    QVERIFY(m_right->isActive());

    // Back in the left toolbar: the left view only.
    clickLocal(m_left, QStringLiteral("go_back"));
    QTRY_COMPARE(m_left->url(), m_dir->url());
    QCOMPARE(m_right->url(), m_dir->url());
    QVERIFY(m_left->isActive());

    // Without a click (no activation first): still the toolbar's own view.
    m_right->setUrl(QUrl::fromLocalFile(path(QStringLiteral("sub2"))));
    QTRY_COMPARE(m_right->url().fileName(), QStringLiteral("sub2"));
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    local(m_right, QStringLiteral("go_up"))->trigger();
    QTRY_COMPARE(m_right->url().adjusted(QUrl::StripTrailingSlash), m_dir->url());
    QCOMPARE(m_left->url(), m_dir->url());

    // The window's Back (its shortcut) acts on the active view.
    m_left->setUrl(QUrl::fromLocalFile(path(QStringLiteral("sub"))));
    QTRY_COMPARE(m_left->url().fileName(), QStringLiteral("sub"));
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QTest::keyClick(m_left->view(), Qt::Key_Left, Qt::AltModifier);
    QTRY_COMPARE(m_left->url(), m_dir->url());
    QCOMPARE(m_right->url().adjusted(QUrl::StripTrailingSlash), m_dir->url()); // untouched

    // Reload of the right view: its own listing, not the left one. (First let the left view
    // finish its navigation: an active view takes the focus after a changed location,
    // asynchronously, and that would activate it again after the click.)
    QTRY_VERIFY(m_left->isAncestorOf(QApplication::focusWidget()));
    QTest::qWait(50);
    QFile::remove(path(QStringLiteral("a.txt")));
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    clickLocal(m_right, QStringLiteral("view_redisplay"));
    QTRY_COMPARE(m_right->view()->itemsCount(), 2);
    QVERIFY(m_right->isActive());
}

void WhaleFileViewToolBarTest::testIndependentNavigationState()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    // Left: two steps of history; right: none.
    m_left->setUrl(QUrl::fromLocalFile(path(QStringLiteral("sub"))));
    QTRY_COMPARE(m_left->url().fileName(), QStringLiteral("sub"));
    QTRY_VERIFY(local(m_left, QStringLiteral("go_back"))->isEnabled());
    QVERIFY(!local(m_left, QStringLiteral("go_forward"))->isEnabled());
    QVERIFY(!local(m_right, QStringLiteral("go_back"))->isEnabled());
    QVERIFY(!local(m_right, QStringLiteral("go_forward"))->isEnabled());

    local(m_left, QStringLiteral("go_back"))->trigger();
    QTRY_COMPARE(m_left->url(), m_dir->url());
    QTRY_VERIFY(local(m_left, QStringLiteral("go_forward"))->isEnabled());
    QVERIFY(!local(m_left, QStringLiteral("go_back"))->isEnabled());
    QVERIFY(!local(m_right, QStringLiteral("go_forward"))->isEnabled());

    // Up: not at the root.
    m_right->setUrl(QUrl::fromLocalFile(QStringLiteral("/")));
    QTRY_VERIFY(!local(m_right, QStringLiteral("go_up"))->isEnabled());
    QVERIFY(local(m_left, QStringLiteral("go_up"))->isEnabled());

    // The history menu of the left Back lists the left view's history only.
    local(m_left, QStringLiteral("go_forward"))->trigger();
    QTRY_COMPARE(m_left->url().fileName(), QStringLiteral("sub"));
    auto back = qobject_cast<KToolBarPopupAction *>(local(m_left, QStringLiteral("go_back")));
    QVERIFY(back);
    Q_EMIT back->popupMenu()->aboutToShow();
    QCOMPARE(back->popupMenu()->actions().count(), 1);
    back->popupMenu()->actions().first()->trigger();
    QTRY_COMPARE(m_left->url(), m_dir->url());
}

void WhaleFileViewToolBarTest::testViewSettingsPerView()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    m_left->view()->setViewMode(DolphinView::DetailsView);
    WhaleViewPlugin *treemap = WhaleViewServices::of(m_mainWindow.data())->registry()->plugin(treemapId);
    QVERIFY(treemap);
    QPointer<WhaleFileViewToolBar> rightBar = toolBar(m_right);
    QVERIFY(m_right->viewHost()->showPluginView(treemapId, treemap));
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());

    auto menuOf = [](DolphinViewContainer *container) {
        auto menu = qobject_cast<KActionMenu *>(local(container, QStringLiteral("view_settings")));
        return menu ? menu->menu()->actions() : QList<QAction *>();
    };
    WhaleViewActions *leftActions = toolBar(m_left)->scope()->viewActions();
    WhaleViewActions *rightActions = toolBar(m_right)->scope()->viewActions();
    // Each menu holds the view's own actions.
    QVERIFY(menuOf(m_left).contains(local(m_left, QStringLiteral("show_preview"))));
    QVERIFY(!menuOf(m_left).contains(global(QStringLiteral("show_preview"))));
    QVERIFY(reachableActions(menuOf(m_right)).contains(rightActions->viewModeAction(treemapId)));

    // Left: Details checked, options enabled. Right: Cushion Treemap checked, the options it
    // does not support disabled.
    QTRY_VERIFY(local(m_left, QStringLiteral("details"))->isChecked());
    QVERIFY(!leftActions->viewModeAction(treemapId)->isChecked());
    QTRY_VERIFY(rightActions->viewModeAction(treemapId)->isChecked());
    for (const QString &name : {QStringLiteral("show_hidden_files"), QStringLiteral("show_preview"), QStringLiteral("whale_include_subfolders")}) {
        QVERIFY2(!local(m_right, name)->isEnabled(), qPrintable(name));
        QVERIFY2(local(m_left, name)->isEnabled(), qPrintable(name));
    }
    for (const QString &name : {QStringLiteral("sort"), KStandardAction::name(KStandardAction::ZoomIn)}) {
        QVERIFY2(!local(m_right, name)->isEnabled(), qPrintable(name));
        QVERIFY2(local(m_left, name)->isEnabled(), qPrintable(name));
    }
    // The window's actions show the active (left) view.
    QVERIFY(global(QStringLiteral("show_hidden_files"))->isEnabled());
    // The navigation of the treemap view stays available.
    QVERIFY(local(m_right, QStringLiteral("view_redisplay"))->isEnabled());

    // Previews toggled in the left toolbar: the left view only.
    const bool rightPreviews = m_right->view()->previewsShown();
    local(m_left, QStringLiteral("show_preview"))->trigger();
    QCOMPARE(local(m_left, QStringLiteral("show_preview"))->isChecked(), m_left->view()->previewsShown());
    QCOMPARE(m_right->view()->previewsShown(), rightPreviews);

    // Back to the file list from the right toolbar: the same toolbar, the file list's state.
    local(m_right, QStringLiteral("icons"))->trigger();
    QTRY_VERIFY(m_right->viewHost()->isDolphinViewShown());
    QCOMPARE(toolBar(m_right), rightBar.data());
    QTRY_VERIFY(local(m_right, QStringLiteral("show_hidden_files"))->isEnabled());
    QVERIFY(local(m_right, QStringLiteral("icons"))->isChecked());
    QVERIFY(!rightActions->viewModeAction(treemapId)->isChecked());
    QVERIFY(m_left->viewHost()->isDolphinViewShown());
    QVERIFY(local(m_left, QStringLiteral("details"))->isChecked());
}

void WhaleFileViewToolBarTest::testViewSettingsMenuIsPopup()
{
    // The second view is created while the window is active; its menu must still be a popup
    // (DolphinViewActionHandler parents it to the active window, which drops the popup type).
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QVERIFY(QApplication::activeWindow());
    for (DolphinViewContainer *container : {m_left, m_right}) {
        QMenu *menu = qobject_cast<KActionMenu *>(local(container, QStringLiteral("view_settings")))->menu();
        QCOMPARE(menu->windowType(), Qt::Popup);
        QCOMPARE(menu->parentWidget(), m_mainWindow.data());
    }
    // Opens from the right toolbar's button. (Not under Wayland: a popup there needs real
    // input, which the dev MCP's acceptance on Wayland uses.)
    if (QGuiApplication::platformName() == QLatin1String("wayland")) {
        return;
    }
    auto button = qobject_cast<QToolButton *>(toolBar(m_right)->widgetForAction(local(m_right, QStringLiteral("view_settings"))));
    QMenu *menu = qobject_cast<KActionMenu *>(local(m_right, QStringLiteral("view_settings")))->menu();
    bool shown = false;
    QTimer::singleShot(200, this, [&shown, menu]() {
        shown = menu->isVisible() && menu->isWindow();
        menu->hide();
    });
    button->showMenu();
    QVERIFY(shown);
}

void WhaleFileViewToolBarTest::testNoAmbiguousShortcut()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
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
    QTest::keyClick(m_left->view(), Qt::Key_F5); // view_redisplay
    QTest::qWait(600);
    QVERIFY(!dialogShown);
}

void WhaleFileViewToolBarTest::testThirdContainer()
{
    // A file view outside of the tab pages, as a dock of the window would hold it.
    QVERIFY(showMainWindow());
    auto dock = new QDockWidget(QStringLiteral("Third view"), m_mainWindow.data());
    auto third = new DolphinViewContainer(QUrl::fromLocalFile(path(QStringLiteral("sub"))), dock);
    dock->setWidget(third);
    m_mainWindow->addDockWidget(Qt::BottomDockWidgetArea, dock);
    third->show();
    dock->show();
    QTRY_VERIFY(toolBar(third) && toolBar(third)->scope());
    QCOMPARE(layoutOf(toolBar(third)), defaultLayout);
    QCOMPARE(toolBar(third)->scope()->viewActions()->services(), WhaleViewServices::of(m_mainWindow.data()));
    QCOMPARE(m_mainWindow->findChildren<WhaleToolbarManager *>().count(), 1);
    QTRY_COMPARE(third->view()->itemsCount(), 1);

    DolphinViewContainer *tabView = m_mainWindow->activeViewContainer();
    local(third, QStringLiteral("go_up"))->trigger();
    QTRY_COMPARE(third->url().adjusted(QUrl::StripTrailingSlash), m_dir->url());
    QCOMPARE(tabView->url(), m_dir->url());
    local(third, QStringLiteral("go_back"))->trigger();
    QTRY_COMPARE(third->url().fileName(), QStringLiteral("sub"));

    QPointer<WhaleFileViewToolBar> bar = toolBar(third);
    delete dock;
    QVERIFY(!bar);
}

void WhaleFileViewToolBarTest::testSessionRestore()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    m_right->setUrl(QUrl::fromLocalFile(path(QStringLiteral("sub2"))));
    QTRY_COMPARE(m_right->url().fileName(), QStringLiteral("sub2"));
    KConfig config(QString(), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Session"));
    // The tabs' session data, as DolphinMainWindow::saveProperties()/readProperties() use it.
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->saveProperties(group);
    m_mainWindow.reset();

    m_mainWindow.reset(new DolphinMainWindow());
    m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->readProperties(group);
    m_mainWindow->resize(1200, 700);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowActive(m_mainWindow.data()));
    QTRY_VERIFY(tabPage()->splitViewEnabled());
    DolphinViewContainer *restored = tabPage()->secondaryViewContainer();
    QTRY_VERIFY(toolBar(restored) && toolBar(restored)->scope());
    QTRY_COMPARE(m_mainWindow->findChildren<WhaleFileViewToolBar *>().count(), 2);
    QTRY_COMPARE(restored->url().fileName(), QStringLiteral("sub2"));
    local(restored, QStringLiteral("go_up"))->trigger();
    QTRY_COMPARE(restored->url().adjusted(QUrl::StripTrailingSlash), m_dir->url());
}

void WhaleFileViewToolBarTest::testClosingWindow()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    QPointer<WhaleFileViewToolBar> leftBar = toolBar(m_left);
    QPointer<WhaleViewCommandScope> rightScope = toolBar(m_right)->scope();
    QPointer<QAction> rightSettings = local(m_right, QStringLiteral("view_settings"));
    QPointer<WhaleToolbarManager> manager = WhaleToolbarManager::of(m_mainWindow.data());
    QPointer<WhaleViewServices> services = WhaleViewServices::of(m_mainWindow.data());
    QVERIFY(leftBar && rightScope && rightSettings && manager && services);
    m_mainWindow.reset();
    QVERIFY(!leftBar);
    QVERIFY(!rightScope);
    QVERIFY(!rightSettings);
    QVERIFY(!manager);
    QVERIFY(!services);
}

QTEST_MAIN(WhaleFileViewToolBarTest)

#include "whalefileviewtoolbartest.moc"
