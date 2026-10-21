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
#include "dolphinviewbridge.h"
#include "dolphinviewcontainer.h"
#include "global.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalecontainercommands.h"
#include "whalecontentscope.h"
#include "whalefileoperationsimpl.h"
#include "whalefileviewtoolbarclient.h"
#include "whalelocationbar.h"
#include "whaleselection.h"
#include "whalesplitviewbutton.h"
#include "whaletoolbarmanager.h"
#include "whaletoolbarsettings.h"
#include "whaleview.h"
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
#include <KMessageWidget>
#include <KSharedConfig>
#include <KStandardAction>
#include <KToolBarPopupAction>

#include <QAccessible>
#include <QClipboard>
#include <QDialog>
#include <QDockWidget>
#include <QDomDocument>
#include <QMenu>
#include <QSignalSpy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>

#include <algorithm>

#include <unistd.h>

namespace
{
const QString treemapId = QStringLiteral("whale_treemap");
const QStringList defaultLayout = {QStringLiteral("go_back"),
                                   QStringLiteral("go_forward"),
                                   QStringLiteral("go_up"),
                                   QStringLiteral("view_redisplay"),
                                   QStringLiteral("whale_location_bar"),
                                   QStringLiteral("view_settings")};

// The actions on selected items that need a selection.
const QStringList selectionActions = {QStringLiteral("renamefile"),
                                      QStringLiteral("movetotrash"),
                                      QStringLiteral("deletefile"),
                                      QStringLiteral("duplicate"),
                                      QStringLiteral("copy_location")};

QVariantMap treemapState(QWidget *view)
{
    QVariantMap state;
    QMetaObject::invokeMethod(view, "treemapState", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, state));
    return state;
}

// Opening the split view animates the width of the new view (DolphinTabPage's expand
// animation, a direct child of the splitter); until it ends, the view may be too narrow for
// all buttons of its File View Toolbar.
bool splitAnimating(DolphinTabPage *page)
{
    auto splitter = page ? qobject_cast<QSplitter *>(page->primaryViewContainer()->parentWidget()) : nullptr;
    if (!splitter) {
        return false;
    }
    const auto animations = splitter->findChildren<QVariantAnimation *>(QString(), Qt::FindDirectChildrenOnly);
    return std::any_of(animations.cbegin(), animations.cend(), [](const QVariantAnimation *animation) {
        return animation->state() == QAbstractAnimation::Running;
    });
}

// The message a view area shows in its message area.
QString messageText(DolphinViewContainer *container)
{
    const auto widgets = container->findChildren<KMessageWidget *>();
    for (KMessageWidget *widget : widgets) {
        if (widget->isVisible() || widget->isShowAnimationRunning()) {
            return widget->text();
        }
    }
    return {};
}

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
 * The File View Toolbar (multi-toolbar plan, TB2 and TB3): one toolbar per file view, acting
 * on that view only, with the complete set of actions such a toolbar can show.
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

    // TB3: the complete set of actions
    void testAllowedActions();
    void testIndependentViewState();
    void testPluginGates();
    void testSelectionPerView();
    void testDeepSelection();
    void testDisabledReasons();
    void testFilterPerView();
    void testSearchPerView();
    void testSplitButton();
    void testSplitButtonWhenLeftCloses();
    void testSplitButtonNarrow();
    void testSplitButtonContextMenu();
    void testMiddleClickNavigation();
    void testReloadAnnouncement();
    void testStopState();
    void testMenusArePopups();

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
    void writeUserLayout(const QStringList &actions);
    static void select(DolphinViewContainer *container, const QStringList &paths);
    QWidget *showTreemap(DolphinViewContainer *container);
    QUrl url(const QString &relative) const;

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
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain({"whale"});
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
    // No questions when trashing or deleting.
    KConfigGroup confirmations(KSharedConfig::openConfig(QStringLiteral("kiorc"), KConfig::NoGlobals), QStringLiteral("Confirmations"));
    confirmations.writeEntry("ConfirmDelete", false);
    confirmations.writeEntry("ConfirmTrash", false);
    confirmations.sync();
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
    GeneralSettings::self()->setGlobalViewProps(true);
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
    QTRY_VERIFY(!splitAnimating(tabPage()));
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
        if (WhaleFileViewToolBar::isLeadingPlaceholder(action)) {
            continue; // not part of the layout, as the lock of a split view on it
        }
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

QUrl WhaleFileViewToolBarTest::url(const QString &relative) const
{
    return QUrl::fromLocalFile(path(relative));
}

void WhaleFileViewToolBarTest::writeUserLayout(const QStringList &actions)
{
    const QString file = localLayoutFile();
    QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
    QFile local(file);
    QVERIFY(local.open(QIODevice::WriteOnly));
    QByteArray content =
        "<!DOCTYPE gui SYSTEM \"kpartgui.dtd\">\n<gui name=\"whale_fileview\" version=\"1\">"
        "<ToolBar noMerge=\"1\" name=\"whaleFileViewToolBar\"><text>File View Toolbar</text>";
    for (const QString &action : actions) {
        content += "<Action name=\"" + action.toUtf8() + "\"/>";
    }
    local.write(content + "</ToolBar></gui>");
    local.close();
    // As at a start: the window reads the file when it is created.
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleFileViewToolBarTest::select(DolphinViewContainer *container, const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }
    container->viewHost()->bridge()->setSelection(WhaleSelection(urls, urls.isEmpty() ? QUrl() : urls.first()));
}

QWidget *WhaleFileViewToolBarTest::showTreemap(DolphinViewContainer *container)
{
    WhaleViewPlugin *plugin = WhaleViewServices::of(m_mainWindow.data())->registry()->plugin(treemapId);
    if (!plugin || !container->viewHost()->showPluginView(treemapId, plugin)) {
        return nullptr;
    }
    QWidget *view = container->viewHost()->currentView()->widget();
    if (!QTest::qWaitFor(
            [view]() {
                return treemapState(view).value(QStringLiteral("status")).toString() == QLatin1String("ready");
            },
            5000)) {
        return nullptr;
    }
    return view;
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
    // As at a start: the window reads the file when it is created.
    m_mainWindow.reset(new DolphinMainWindow());
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
    const QStringList names = WhaleViewCommandScope::allowedActions();
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
    QVERIFY(!global(KStandardAction::name(KStandardAction::RenameFile))->shortcuts().isEmpty());
    QVERIFY(!global(KStandardAction::name(KStandardAction::SelectAll))->shortcuts().isEmpty());
    QVERIFY(!global(QStringLiteral("show_filter_bar"))->shortcuts().isEmpty());
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

void WhaleFileViewToolBarTest::testAllowedActions()
{
    // A toolbar that shows every allowed action, and some that are not allowed.
    const QStringList allowed = WhaleViewCommandScope::allowedActions();
    const QStringList notAllowed = {
        QStringLiteral("url_navigators"),
        QStringLiteral("replace_location"),
        QStringLiteral("editable_location"),
        QStringLiteral("split_view"),
        QStringLiteral("split_stash"),
        QStringLiteral("split_view_menu"),
        QStringLiteral("popout_split_view"),
        QStringLiteral("hamburger_menu"),
        QStringLiteral("compare_files"),
        QStringLiteral("copy_to_inactive_split_view"),
        QStringLiteral("move_to_inactive_split_view"),
        QStringLiteral("focus_inactive_split_view"),
        QStringLiteral("edit_undo"),
        QStringLiteral("edit_redo"),
        QStringLiteral("new_tab"),
        QStringLiteral("file_close"),
        QStringLiteral("edit_cut"),
        QStringLiteral("edit_copy"),
        QStringLiteral("edit_paste"),
        QStringLiteral("create_dir"),
        QStringLiteral("create_file"),
        QStringLiteral("new_menu"),
        QStringLiteral("toggle_selection_mode"),
        QStringLiteral("toggle_selection_mode_tool_bar"),
        QStringLiteral("open_terminal"),
        QStringLiteral("open_terminal_here"),
        QStringLiteral("open_preferred_search_tool"),
        QStringLiteral("manage_disk_space"),
        QStringLiteral("basic_actions"),
        QStringLiteral("delete_shortcut"),
    };
    writeUserLayout(allowed + notAllowed);
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    QCOMPARE(allowed.count(), 36);
    QCOMPARE(QSet<QString>(allowed.cbegin(), allowed.cend()).count(), allowed.count());
    // One definition: the toolbars' descriptor offers exactly this set.
    // ... plus the location bar (location bar plan, LB2).
    QCOMPARE(WhaleToolbarManager::of(m_left)->descriptor(WhaleToolbarDescriptor::fileViewId()).allowedActions,
             allowed + QStringList{QStringLiteral("whale_location_bar")});

    for (const QString &name : allowed) {
        // The names are those of the window's actions; each view has objects of its own.
        QVERIFY2(global(name), qPrintable(name));
        QAction *first = local(m_left, name);
        QAction *second = local(m_right, name);
        QVERIFY2(first && second, qPrintable(name));
        QVERIFY2(first != second && first != global(name) && second != global(name), qPrintable(name));
        QCOMPARE(first->objectName(), name);
        // Text and icon, as the configuration of the toolbars will list them.
        QVERIFY2(!first->text().isEmpty(), qPrintable(name));
    }
    for (const QString &name : notAllowed) {
        QVERIFY2(!local(m_left, name), qPrintable(name));
        QVERIFY2(!local(m_right, name), qPrintable(name));
    }
    // The toolbars show all allowed actions and nothing else, both the same.
    QCOMPARE(layoutOf(toolBar(m_left)), allowed);
    QCOMPARE(layoutOf(toolBar(m_right)), allowed);
    for (DolphinViewContainer *container : {m_left, m_right}) {
        const auto shown = toolBar(container)->actions();
        for (QAction *action : shown) {
            QVERIFY2(!m_mainWindow->actionCollection()->actions().contains(action), qPrintable(action->objectName()));
        }
    }
}

void WhaleFileViewToolBarTest::testIndependentViewState()
{
    // Each folder with view properties of its own; with Dolphin's "same view for all
    // folders" the views share them, which is not the toolbars' business.
    GeneralSettings::self()->setGlobalViewProps(false);
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    m_left->setUrl(url(QStringLiteral("sub")));
    m_right->setUrl(url(QStringLiteral("sub2")));
    QTRY_COMPARE(m_left->view()->itemsCount(), 1);
    QTRY_COMPARE(m_right->view()->itemsCount(), 1);
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    const QString restore = QStringLiteral("restore_view_settings_default");
    QTRY_VERIFY(!local(m_left, restore)->isEnabled());
    QVERIFY(!local(m_right, restore)->isEnabled());

    // The view mode, chosen in the toolbar of the view that is not active.
    const DolphinView::Mode firstMode = m_left->view()->viewMode();
    QAction *otherMode = local(m_right, firstMode == DolphinView::DetailsView ? QStringLiteral("compact") : QStringLiteral("details"));
    otherMode->trigger();
    QTRY_VERIFY(m_right->view()->viewMode() != firstMode);
    QCOMPARE(m_left->view()->viewMode(), firstMode);
    QVERIFY(otherMode->isChecked());
    QVERIFY(!local(m_left, otherMode->objectName())->isChecked());
    QVERIFY(!global(otherMode->objectName())->isChecked()); // the window's: the active view
    // Restore to Defaults follows the view's own settings.
    QTRY_VERIFY(local(m_right, restore)->isEnabled());
    QVERIFY(!local(m_left, restore)->isEnabled());

    // Previews, hidden files and the flat file list.
    for (const QString &name : {QStringLiteral("show_preview"), QStringLiteral("show_hidden_files"), QStringLiteral("whale_include_subfolders")}) {
        const bool before = local(m_left, name)->isChecked();
        QCOMPARE(local(m_right, name)->isChecked(), before);
        local(m_right, name)->trigger();
        QTRY_VERIFY2(local(m_right, name)->isChecked() != before, qPrintable(name));
        QVERIFY2(local(m_left, name)->isChecked() == before, qPrintable(name));
    }
    QCOMPARE(m_right->view()->previewsShown(), local(m_right, QStringLiteral("show_preview"))->isChecked());
    QVERIFY(m_right->view()->previewsShown() != m_left->view()->previewsShown());
    QVERIFY(m_right->view()->hiddenFilesShown() != m_left->view()->hiddenFilesShown());
    QCOMPARE(m_right->viewHost()->contentScope(), WhaleContentScope::RecursiveFiles);
    QCOMPARE(m_left->viewHost()->contentScope(), WhaleContentScope::CurrentFolder);
    local(m_right, QStringLiteral("whale_include_subfolders"))->trigger();
    QTRY_COMPARE(m_right->viewHost()->contentScope(), WhaleContentScope::CurrentFolder);

    // Sorting and grouping.
    for (const QString &name : {QStringLiteral("folders_first"), QStringLiteral("hidden_last")}) {
        const bool before = local(m_left, name)->isChecked();
        local(m_right, name)->trigger();
        QTRY_VERIFY2(local(m_right, name)->isChecked() != before, qPrintable(name));
        QVERIFY2(local(m_left, name)->isChecked() == before, qPrintable(name));
    }
    QVERIFY(m_right->view()->sortFoldersFirst() != m_left->view()->sortFoldersFirst());
    QAction *descending = toolBar(m_right)->scope()->actionCollection()->action(QStringLiteral("descending"));
    QVERIFY(reachableActions({local(m_right, QStringLiteral("sort"))}).contains(descending));
    descending->trigger();
    QTRY_COMPARE(m_right->view()->sortOrder(), Qt::DescendingOrder);
    QCOMPARE(m_left->view()->sortOrder(), Qt::AscendingOrder);
    QVERIFY(toolBar(m_left)->scope()->actionCollection()->action(QStringLiteral("ascending"))->isChecked());
    const bool grouped = m_left->view()->groupedSorting();
    m_right->view()->setGroupedSorting(!grouped);
    QCOMPARE(m_left->view()->groupedSorting(), grouped);
    QTRY_VERIFY(toolBar(m_right)->scope()->actionCollection()->action(QStringLiteral("group_by_none"))->isChecked() == grouped);
    QVERIFY(toolBar(m_left)->scope()->actionCollection()->action(QStringLiteral("group_by_none"))->isChecked() != grouped);

    // Zoom: the limits of the own view.
    const QString zoomIn = KStandardAction::name(KStandardAction::ZoomIn);
    QVERIFY(local(m_left, zoomIn)->isEnabled() && local(m_right, zoomIn)->isEnabled());
    for (int step = 0; step < 30 && local(m_right, zoomIn)->isEnabled(); ++step) {
        local(m_right, zoomIn)->trigger();
    }
    QVERIFY(!local(m_right, zoomIn)->isEnabled());
    QVERIFY(local(m_left, zoomIn)->isEnabled());
    QVERIFY(m_right->view()->zoomLevel() > m_left->view()->zoomLevel());
    local(m_right, QStringLiteral("view_zoom_reset"))->trigger();
    QTRY_VERIFY(local(m_right, zoomIn)->isEnabled());

    // Back to the defaults in the toolbar of the changed view.
    local(m_right, restore)->trigger();
    QTRY_VERIFY(!local(m_right, restore)->isEnabled());
    QCOMPARE(m_right->view()->viewMode(), firstMode);
    QVERIFY(m_left->isActive());
}

void WhaleFileViewToolBarTest::testPluginGates()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    const QStringList gated = {QStringLiteral("show_hidden_files"),
                               QStringLiteral("show_preview"),
                               QStringLiteral("whale_include_subfolders"),
                               QStringLiteral("sort"),
                               QStringLiteral("folders_first"),
                               QStringLiteral("hidden_last"),
                               QStringLiteral("group_by"),
                               QStringLiteral("additional_info"),
                               QStringLiteral("view_zoom_in"),
                               QStringLiteral("view_zoom_out"),
                               QStringLiteral("view_zoom_reset"),
                               QStringLiteral("edit_select_all"),
                               QStringLiteral("invert_selection")};
    auto states = [](DolphinViewContainer *container) {
        QHash<QString, QPair<bool, bool>> result;
        const QStringList names = WhaleViewCommandScope::allowedActions();
        for (const QString &name : names) {
            result.insert(name, {local(container, name)->isEnabled(), local(container, name)->isChecked()});
        }
        return result;
    };
    m_left->view()->setViewMode(DolphinView::DetailsView);
    QTRY_VERIFY(local(m_left, QStringLiteral("details"))->isChecked());
    const auto leftBefore = states(m_left);
    const auto rightBefore = states(m_right);
    // (All but one: the file list starts at a limit of its zoom.)
    QVERIFY(std::ranges::count_if(gated,
                                  [&](const QString &name) {
                                      return leftBefore.value(name).first && rightBefore.value(name).first;
                                  })
            >= gated.count() - 1);

    // Several times: the toolbar and its actions stay, their state follows the shown view.
    QPointer<WhaleFileViewToolBar> rightBar = toolBar(m_right);
    QPointer<QAction> rightHidden = local(m_right, QStringLiteral("show_hidden_files"));
    for (int round = 0; round < 3; ++round) {
        QVERIFY(showTreemap(m_right));
        for (const QString &name : gated) {
            QTRY_VERIFY2(!local(m_right, name)->isEnabled(), qPrintable(name));
        }
        // Disabled, not hidden.
        for (const QString &name : WhaleViewCommandScope::allowedActions()) {
            QVERIFY2(local(m_right, name)->isVisible(), qPrintable(name));
        }
        // The other view is untouched, whichever is active.
        QCOMPARE(states(m_left), leftBefore);
        m_right->setActive(true);
        QTRY_VERIFY(m_right->isActive());
        QCOMPARE(states(m_left), leftBefore);
        m_left->setActive(true);
        QTRY_VERIFY(m_left->isActive());
        QCOMPARE(states(m_left), leftBefore);
        // What a treemap does not disable.
        for (const QString &name : {QStringLiteral("view_redisplay"),
                                    QStringLiteral("go_up"),
                                    QStringLiteral("view_settings"),
                                    QStringLiteral("view_mode"),
                                    QStringLiteral("icons")}) {
            QVERIFY2(local(m_right, name)->isEnabled(), qPrintable(name));
        }

        m_right->viewHost()->showDolphinView();
        QTRY_COMPARE(states(m_right), rightBefore);
        QCOMPARE(states(m_left), leftBefore);
    }
    QCOMPARE(toolBar(m_right), rightBar.data());
    QCOMPARE(local(m_right, QStringLiteral("show_hidden_files")), rightHidden.data());
}

void WhaleFileViewToolBarTest::testSelectionPerView()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    m_right->setUrl(url(QStringLiteral("sub2")));
    QTRY_COMPARE(m_right->view()->itemsCount(), 1);
    auto enabled = [](DolphinViewContainer *container) {
        QStringList result;
        for (const QString &name : selectionActions) {
            if (local(container, name)->isEnabled()) {
                result.append(name);
            }
        }
        return result;
    };
    // Nothing selected: disabled in both (the window's actions would start the selection
    // mode); Properties is about the location then.
    QCOMPARE(enabled(m_left), QStringList());
    QCOMPARE(enabled(m_right), QStringList());
    QVERIFY(local(m_left, QStringLiteral("properties"))->isEnabled());
    QVERIFY(local(m_right, QStringLiteral("properties"))->isEnabled());

    // A selection in one view only, whichever view is active.
    for (DolphinViewContainer *active : {m_left, m_right}) {
        active->setActive(true);
        QTRY_VERIFY(active->isActive());
        select(m_left, {path(QStringLiteral("a.txt"))});
        QTRY_COMPARE(enabled(m_left), selectionActions);
        QCOMPARE(enabled(m_right), QStringList());
        QVERIFY(active->isActive());

        select(m_left, {});
        select(m_right, {path(QStringLiteral("sub2/c.txt"))});
        QTRY_COMPARE(enabled(m_right), selectionActions);
        QCOMPARE(enabled(m_left), QStringList());
        QVERIFY(active->isActive());
        select(m_right, {});
        QTRY_COMPARE(enabled(m_right), QStringList());
    }

    // Several items: no single location to copy.
    select(m_left, {path(QStringLiteral("a.txt")), path(QStringLiteral("sub"))});
    QTRY_VERIFY(local(m_left, QStringLiteral("renamefile"))->isEnabled());
    QVERIFY(!local(m_left, QStringLiteral("copy_location"))->isEnabled());

    // The actions act on the selection of their own view; the other view is active and has
    // a selection of its own.
    select(m_left, {path(QStringLiteral("a.txt"))});
    select(m_right, {path(QStringLiteral("sub2/c.txt"))});
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QTRY_COMPARE(enabled(m_right), selectionActions);
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) { // not the user's clipboard
        local(m_right, QStringLiteral("copy_location"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), path(QStringLiteral("sub2/c.txt")));
    }
    local(m_right, QStringLiteral("duplicate"))->trigger();
    QTRY_COMPARE(QDir(path(QStringLiteral("sub2"))).entryList(QDir::Files).count(), 2);
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files).count(), 1);

    select(m_right, {path(QStringLiteral("sub2/c.txt"))});
    QTRY_VERIFY(local(m_right, QStringLiteral("movetotrash"))->isEnabled());
    local(m_right, QStringLiteral("movetotrash"))->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("sub2/c.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("a.txt"))));
    QCOMPARE(m_left->view()->selectedItems().count(), 1);
    QVERIFY(m_left->isActive());

    // Select All and Invert Selection of the view that is not active.
    local(m_right, QStringLiteral("edit_select_all"))->trigger();
    QTRY_COMPARE(m_right->view()->selectedItemsCount(), m_right->view()->itemsCount());
    QCOMPARE(m_left->view()->selectedItemsCount(), 1);
    local(m_right, QStringLiteral("invert_selection"))->trigger();
    QTRY_COMPARE(m_right->view()->selectedItemsCount(), 0);
    QCOMPARE(m_left->view()->selectedItemsCount(), 1);
}

void WhaleFileViewToolBarTest::testDeepSelection()
{
    // A file list with a selection, and a treemap whose own selection is deep below its
    // location: the treemap's actions act on that one.
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    select(m_left, {path(QStringLiteral("a.txt"))});
    QWidget *treemap = showTreemap(m_right);
    QVERIFY(treemap);
    // Nothing selected in the treemap yet.
    for (const QString &name : selectionActions) {
        QTRY_VERIFY2(!local(m_right, name)->isEnabled(), qPrintable(name));
    }
    QTRY_VERIFY(local(m_left, QStringLiteral("movetotrash"))->isEnabled());

    const QUrl deep = url(QStringLiteral("sub/b.txt"));
    QRect cell;
    QMetaObject::invokeMethod(treemap, "cellRect", Qt::DirectConnection, Q_RETURN_ARG(QRect, cell), Q_ARG(QUrl, deep));
    QVERIFY(!cell.isEmpty());
    QWidget *canvas = treemap->findChild<QWidget *>(QStringLiteral("whaleTreemapCanvas"));
    QTest::mouseClick(canvas, Qt::LeftButton, {}, cell.center());
    for (int step = 0; step < 10 && QUrl(treemapState(treemap).value(QStringLiteral("selected")).toString()) != deep; ++step) {
        QTest::keyClick(canvas, Qt::Key_Up);
    }
    QCOMPARE(QUrl(treemapState(treemap).value(QStringLiteral("selected")).toString()), deep);
    QCOMPARE(m_right->viewHost()->currentView()->selectedItems(), QList<QUrl>({deep}));
    // The click made the treemap's view the active one; the file list shall be.
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());

    for (const QString &name : {QStringLiteral("renamefile"), QStringLiteral("movetotrash"), QStringLiteral("deletefile"), QStringLiteral("copy_location")}) {
        QTRY_VERIFY2(local(m_right, name)->isEnabled(), qPrintable(name));
    }
    QVERIFY(local(m_right, QStringLiteral("properties"))->isEnabled());
    // No destination among the items of many folders: Duplicate stays disabled there, and
    // the toolbar can tell why. In the file list it is available.
    QVERIFY(!local(m_right, QStringLiteral("duplicate"))->isEnabled());
    QCOMPARE(toolBar(m_right)->scope()->viewActions()->gateReason(QStringLiteral("duplicate")), QStringLiteral("no destination folder"));
    QCOMPARE(toolBar(m_right)->scope()->disabledReason(local(m_right, QStringLiteral("duplicate"))),
             m_right->viewHost()->refusal(WhaleViewHost::Refusal::Create));
    QVERIFY(local(m_left, QStringLiteral("duplicate"))->isEnabled());
    QVERIFY(toolBar(m_left)->scope()->disabledReason(local(m_left, QStringLiteral("duplicate"))).isEmpty());

    if (QGuiApplication::platformName() == QLatin1String("offscreen")) { // not the user's clipboard
        local(m_right, QStringLiteral("copy_location"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), deep.toLocalFile());
        local(m_left, QStringLiteral("copy_location"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), path(QStringLiteral("a.txt")));
    }
    // Move to Trash in the treemap's toolbar: the deep item, not the file list's selection.
    // (A treemap asks before it removes items; answered here.)
    QList<QUrl> confirmed;
    WhaleFileOperationsImpl::setConfirmationRunner([&confirmed](WhaleFileOperations::Operation, const QList<QUrl> &urls) {
        confirmed = urls;
        return true;
    });
    local(m_right, QStringLiteral("movetotrash"))->trigger();
    WhaleFileOperationsImpl::setConfirmationRunner({});
    QCOMPARE(confirmed, QList<QUrl>({deep}));
    QTRY_VERIFY(!QFile::exists(deep.toLocalFile()));
    QVERIFY(QFile::exists(path(QStringLiteral("a.txt"))));
    QCOMPARE(m_left->view()->selectedItems().count(), 1);
    // ... and in the file list's toolbar: its own selection.
    local(m_left, QStringLiteral("movetotrash"))->trigger();
    QTRY_VERIFY(!QFile::exists(path(QStringLiteral("a.txt"))));
    QVERIFY(QFile::exists(path(QStringLiteral("sub2/c.txt"))));
}

void WhaleFileViewToolBarTest::testDisabledReasons()
{
    writeUserLayout({QStringLiteral("duplicate"), QStringLiteral("renamefile"), QStringLiteral("show_preview")});
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    auto press = [](DolphinViewContainer *container, const QString &name) {
        QWidget *button = toolBar(container)->widgetForAction(local(container, name));
        QVERIFY(button);
        QVERIFY(!button->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
    };
    // The second view shows its files flat: no folder to duplicate into.
    select(m_left, {path(QStringLiteral("a.txt"))});
    m_right->viewHost()->selectContentScope(WhaleContentScope::RecursiveFiles);
    QTRY_COMPARE(m_right->view()->itemsCount(), 3);
    select(m_right, {path(QStringLiteral("sub/b.txt"))});
    QTRY_VERIFY(local(m_right, QStringLiteral("renamefile"))->isEnabled());
    QVERIFY(!local(m_right, QStringLiteral("duplicate"))->isEnabled());
    QVERIFY(local(m_left, QStringLiteral("duplicate"))->isEnabled());
    const QString refusal = m_right->viewHost()->refusal(WhaleViewHost::Refusal::Create);
    QVERIFY(!refusal.isEmpty());
    QCOMPARE(toolBar(m_right)->scope()->disabledReason(local(m_right, QStringLiteral("duplicate"))), refusal);

    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QVERIFY(messageText(m_left).isEmpty() && messageText(m_right).isEmpty());
    press(m_right, QStringLiteral("duplicate"));
    QTRY_COMPARE(messageText(m_right), refusal);
    QVERIFY(messageText(m_left).isEmpty());

    // Back in the folder's own items the action is available again and explains nothing.
    m_right->viewHost()->selectContentScope(WhaleContentScope::CurrentFolder);
    QTRY_COMPARE(m_right->view()->itemsCount(), 3);
    select(m_right, {path(QStringLiteral("a.txt"))});
    QTRY_VERIFY(local(m_right, QStringLiteral("duplicate"))->isEnabled());
    QVERIFY(toolBar(m_right)->scope()->disabledReason(local(m_right, QStringLiteral("duplicate"))).isEmpty());

    // Nothing selected: disabled without an explanation (as a disabled button of the
    // window's toolbar).
    select(m_left, {});
    QTRY_VERIFY(!local(m_left, QStringLiteral("renamefile"))->isEnabled());
    QVERIFY(toolBar(m_left)->scope()->disabledReason(local(m_left, QStringLiteral("renamefile"))).isEmpty());

    // Dolphin's explanation for items that cannot be renamed, in the view of the toolbar.
    if (geteuid() != 0) {
        const QString readOnly = path(QStringLiteral("sub2"));
        QVERIFY(QFile::setPermissions(readOnly, QFile::ReadOwner | QFile::ExeOwner));
        m_left->setUrl(QUrl::fromLocalFile(readOnly));
        QTRY_COMPARE(m_left->view()->itemsCount(), 1);
        select(m_left, {path(QStringLiteral("sub2/c.txt"))});
        const bool disabled = QTest::qWaitFor(
            [this]() {
                return !toolBar(m_left)->scope()->disabledReason(local(m_left, QStringLiteral("renamefile"))).isEmpty();
            },
            3000);
        const QString reason = toolBar(m_left)->scope()->disabledReason(local(m_left, QStringLiteral("renamefile")));
        if (disabled) {
            m_right->setActive(true);
            press(m_left, QStringLiteral("renamefile"));
        }
        const QString shown = messageText(m_left);
        QFile::setPermissions(readOnly, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(disabled);
        QVERIFY(reason.startsWith(QLatin1String("Cannot rename")));
        QCOMPARE(shown, reason);
        QVERIFY(messageText(m_right) != reason);
    }
}

void WhaleFileViewToolBarTest::testFilterPerView()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    const QString toggle = QStringLiteral("toggle_filter");
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QVERIFY(!m_left->isFilterBarVisible() && !m_right->isFilterBarVisible());
    QVERIFY(local(m_left, toggle)->isCheckable());
    QVERIFY(!local(m_left, toggle)->isChecked() && !local(m_right, toggle)->isChecked());

    local(m_right, toggle)->trigger();
    QTRY_VERIFY(m_right->isFilterBarVisible());
    QVERIFY(!m_left->isFilterBarVisible());
    QVERIFY(local(m_right, toggle)->isChecked());
    QVERIFY(!local(m_left, toggle)->isChecked());

    // The window's action shows the active view's filter bar.
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QTRY_VERIFY(!global(toggle)->isChecked());
    global(toggle)->trigger();
    QTRY_VERIFY(m_left->isFilterBarVisible());
    QVERIFY(local(m_left, toggle)->isChecked());
    QVERIFY(local(m_right, toggle)->isChecked());

    local(m_right, toggle)->trigger();
    QTRY_VERIFY(!m_right->isFilterBarVisible());
    QVERIFY(m_left->isFilterBarVisible());
    QVERIFY(!local(m_right, toggle)->isChecked());
    QVERIFY(local(m_left, toggle)->isChecked());

    // "Filter…" shows the bar of the own view.
    local(m_right, QStringLiteral("show_filter_bar"))->trigger();
    QTRY_VERIFY(m_right->isFilterBarVisible());
    QVERIFY(local(m_right, toggle)->isChecked());
}

void WhaleFileViewToolBarTest::testSearchPerView()
{
    // E8 revised: the File View Toolbar's search button opens and closes the search bar of its
    // own view; the window's one acts on the active view.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    const QString toggle = QStringLiteral("toggle_search");
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QVERIFY(!m_left->isSearchBarVisible() && !m_right->isSearchBarVisible());
    QVERIFY(local(m_left, toggle)->isCheckable());
    QVERIFY(!local(m_left, toggle)->isChecked() && !local(m_right, toggle)->isChecked());

    local(m_right, toggle)->trigger();
    QTRY_VERIFY(m_right->isSearchBarVisible());
    QVERIFY(!m_left->isSearchBarVisible());
    QVERIFY(local(m_right, toggle)->isChecked());
    QVERIFY(!local(m_left, toggle)->isChecked());
    // The focus goes into that search bar (as with the window's action).
    QTRY_VERIFY(m_right->isAncestorOf(QApplication::focusWidget()));
    QVERIFY(!m_right->view()->isAncestorOf(QApplication::focusWidget()));

    // The window's action opens the active view's search bar; the local buttons follow.
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QTRY_VERIFY(!global(toggle)->isChecked());
    global(toggle)->trigger();
    QTRY_VERIFY(m_left->isSearchBarVisible());
    QVERIFY(local(m_left, toggle)->isChecked());
    QVERIFY(local(m_right, toggle)->isChecked());

    local(m_right, toggle)->trigger();
    QTRY_VERIFY(!m_right->isSearchBarVisible());
    QVERIFY(m_left->isSearchBarVisible());
    QVERIFY(!local(m_right, toggle)->isChecked());
    QVERIFY(local(m_left, toggle)->isChecked());
    // No shortcut of its own (as all actions of a File View Toolbar).
    QVERIFY(local(m_left, toggle)->shortcuts().isEmpty());
}

void WhaleFileViewToolBarTest::testSplitButton()
{
    // The split button: only in the File View Toolbar of the rightmost view, at its end,
    // outside of the layout. ">>" in the only view opens the right view, "<<" in the right
    // view closes it: the button stays at the same place in the window.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    WhaleFileViewToolBar *bar = toolBar(container);
    QTRY_VERIFY(bar->scope());
    QToolButton *button = WhaleSplitViewButton::buttonOf(tabPage());
    QVERIFY(button);
    QTRY_VERIFY(button->isVisible());
    QCOMPARE(button->parentWidget(), bar);
    QCOMPARE(bar->trailingWidget(), button);
    QCOMPARE(button->icon().name(), QStringLiteral("arrow-right-double"));
    QVERIFY(!bar->actions().contains(button->defaultAction()) || !button->defaultAction());
    // At the end of the row, after all buttons of the layout.
    for (QAction *action : bar->actions()) {
        if (QWidget *widget = bar->widgetForAction(action); widget && widget->isVisible()) {
            QVERIFY(widget->geometry().right() < button->geometry().left());
        }
    }
    QVERIFY(button->geometry().right() <= bar->width());
    QVERIFY(button->geometry().right() > bar->width() - 2 * button->width());

    // Opens the right view; the button moves into it and points back.
    const int choice = GeneralSettings::closeSplitViewChoice();
    GeneralSettings::self()->setCloseSplitViewChoice(GeneralSettings::EnumCloseSplitViewChoice::ActiveView);
    const auto rightEdge = [button, this]() {
        return button->mapTo(m_mainWindow.data(), QPoint(button->width(), 0)).x();
    };
    const int rightEdgeUnsplit = rightEdge();
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY(tabPage()->splitViewEnabled());
    m_left = tabPage()->primaryViewContainer();
    m_right = tabPage()->secondaryViewContainer();
    QCOMPARE(m_left, container);
    QTRY_VERIFY(toolBar(m_right)->scope());
    QTRY_COMPARE(button->icon().name(), QStringLiteral("arrow-left-double"));
    QCOMPARE(button->parentWidget(), toolBar(m_right));
    QTRY_VERIFY(button->isVisible());
    QVERIFY(!toolBar(m_left)->trailingWidget());
    QVERIFY(!toolBar(m_left)->findChild<QToolButton *>(QStringLiteral("whale_split_view_button")));
    // At the same place in the window as before.
    QTRY_COMPARE(rightEdge(), rightEdgeUnsplit);

    // Closes the right view, also when the settings say "the active view" and the left one is
    // active; the settings stay as they are.
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QPointer<DolphinViewContainer> right = m_right;
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY(!right);
    QVERIFY(!tabPage()->splitViewEnabled());
    QCOMPARE(tabPage()->primaryViewContainer(), container);
    QCOMPARE(GeneralSettings::closeSplitViewChoice(), int(GeneralSettings::EnumCloseSplitViewChoice::ActiveView));
    QTRY_COMPARE(button->icon().name(), QStringLiteral("arrow-right-double"));
    QCOMPARE(button->parentWidget(), bar);
    // The window's split action follows.
    QCOMPARE(KLocalizedString::removeAcceleratorMarker(global(QStringLiteral("split_view"))->iconText()), QStringLiteral("Split"));
    GeneralSettings::self()->setCloseSplitViewChoice(choice);

    // Hidden File View Toolbars: no button. Not offered in Configure Toolbars (no action).
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    QVERIFY(!button->isVisible());
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    QTRY_VERIFY(button->isVisible());
    QVERIFY(!WhaleViewCommandScope::allowedActions().contains(QStringLiteral("split_view")));

    // A new tab has a button of its own.
    global(QStringLiteral("new_tab"))->trigger();
    QTRY_VERIFY(tabPage() && WhaleSplitViewButton::buttonOf(tabPage()) && WhaleSplitViewButton::buttonOf(tabPage()) != button);
    QTRY_VERIFY(WhaleSplitViewButton::buttonOf(tabPage())->isVisible());
    QVERIFY(!button->isVisible());
}

void WhaleFileViewToolBarTest::testSplitButtonWhenLeftCloses()
{
    // The window's button closes the left view (settings: the active view): the right one,
    // which has the button, is the only one now and keeps it.
    const int choice = GeneralSettings::closeSplitViewChoice();
    GeneralSettings::self()->setCloseSplitViewChoice(GeneralSettings::EnumCloseSplitViewChoice::ActiveView);
    showSplit();
    if (QTest::currentTestFailed()) {
        GeneralSettings::self()->setCloseSplitViewChoice(choice);
        return;
    }
    QPointer<QToolButton> button = WhaleSplitViewButton::buttonOf(tabPage());
    QVERIFY(button);
    QTRY_COMPARE(button->parentWidget(), toolBar(m_right));
    QPointer<DolphinViewContainer> left = m_left;
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    global(QStringLiteral("split_view"))->trigger();
    QTRY_VERIFY(!left);
    QVERIFY(button); // not deleted with the view
    QCOMPARE(tabPage()->primaryViewContainer(), m_right);
    QTRY_COMPARE(button->parentWidget(), toolBar(m_right));
    QTRY_VERIFY(button->isVisible());
    QCOMPARE(button->icon().name(), QStringLiteral("arrow-right-double"));
    GeneralSettings::self()->setCloseSplitViewChoice(choice);
}

void WhaleFileViewToolBarTest::testSplitButtonNarrow()
{
    // Always there: in a narrow view, the buttons of the layout go into the overflow menu and
    // the location bar into the second row, the split button stays at the end of the first row.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    WhaleFileViewToolBar *bar = toolBar(container);
    QToolButton *button = WhaleSplitViewButton::buttonOf(tabPage());
    QTRY_VERIFY(button && button->isVisible());
    if (auto places = m_mainWindow->findChild<QDockWidget *>(QStringLiteral("placesDock"))) {
        places->hide();
    }
    m_mainWindow->resize(200, 700);
    QTRY_VERIFY(bar->isLocationBarWrapped());
    QTest::qWait(50);
    QVERIFY(button->isVisible());
    QVERIFY(button->geometry().right() <= bar->width());
    QVERIFY(button->geometry().bottom() < bar->locationBar()->geometry().top());
    for (QAction *action : bar->actions()) {
        if (QWidget *widget = bar->widgetForAction(action); widget && widget->isVisible()) {
            QVERIFY(!widget->geometry().intersects(button->geometry()));
        }
    }
}

void WhaleFileViewToolBarTest::testSplitButtonContextMenu()
{
    // "<<" has a context menu that moves the right view to a new tab or window instead of
    // closing it: the same right view, also when the left one is active.
    showSplit();
    QToolButton *button = WhaleSplitViewButton::buttonOf(tabPage());
    QVERIFY(button);
    QTRY_COMPARE(button->parentWidget(), toolBar(m_right));
    QVERIFY(button->toolTip().contains(QStringLiteral("Right-click")));
    const QUrl leftUrl = m_left->url();
    QVERIFY(QDir(leftUrl.toLocalFile()).mkpath(QStringLiteral("rightFolder")));
    const QUrl rightUrl = QUrl::fromLocalFile(leftUrl.toLocalFile() + QStringLiteral("/rightFolder"));
    m_right->setUrl(rightUrl);
    QTRY_COMPARE(m_right->url(), rightUrl);
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());

    Q_EMIT button->customContextMenuRequested(QPoint(button->width() / 2, button->height() / 2));
    QMenu *menu = button->findChild<QMenu *>();
    QVERIFY(menu);
    auto toTab = menu->findChild<QAction *>(QStringLiteral("whale_split_view_to_tab"));
    QVERIFY(toTab);
    // Not triggered here: a new window is a process of its own.
    QVERIFY(menu->findChild<QAction *>(QStringLiteral("whale_split_view_popout")));
    QCOMPARE(menu->actions().count(), 2);

    auto tabs = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    const int tabCount = tabs->count();
    DolphinTabPage *page = tabPage();
    QPointer<DolphinViewContainer> right = m_right;
    toTab->trigger();
    menu->close();
    QTRY_VERIFY(!right);
    QCOMPARE(tabs->count(), tabCount + 1);
    QVERIFY(!page->splitViewEnabled());
    QCOMPARE(page->primaryViewContainer(), m_left);
    QCOMPARE(m_left->url(), leftUrl);
    QCOMPARE(tabs->tabPageAt(tabs->indexOf(page) + 1)->primaryViewContainer()->url(), rightUrl);

    // Without a split view there is nothing to move: no menu.
    QTRY_VERIFY(!button->findChild<QMenu *>());
    Q_EMIT button->customContextMenuRequested(QPoint(1, 1));
    QVERIFY(!button->findChild<QMenu *>());
}

void WhaleFileViewToolBarTest::testMiddleClickNavigation()
{
    writeUserLayout({QStringLiteral("go_back"), QStringLiteral("go_forward"), QStringLiteral("go_up"), QStringLiteral("go_home")});
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    auto tabWidget = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    // The second view: root, sub, sub2; then back to sub. The first view stays at the root.
    m_right->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(m_right->url().fileName(), QStringLiteral("sub"));
    m_right->setUrl(url(QStringLiteral("sub2")));
    QTRY_COMPARE(m_right->url().fileName(), QStringLiteral("sub2"));
    local(m_right, QStringLiteral("go_back"))->trigger();
    QTRY_COMPARE(m_right->url().fileName(), QStringLiteral("sub"));
    QTRY_VERIFY(m_right->isAncestorOf(QApplication::focusWidget()));
    QTest::qWait(50);
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());

    auto middleClick = [](DolphinViewContainer *container, const QString &name) {
        QWidget *button = toolBar(container)->widgetForAction(local(container, name));
        QVERIFY(button);
        QTest::mouseClick(button, Qt::MiddleButton);
    };
    // The location of the tab opened last (new tabs open next to the current one).
    QList<DolphinTabPage *> known = {tabWidget->tabPageAt(0)};
    auto urlOfNewTab = [tabWidget, &known]() {
        for (int index = 0; index < tabWidget->count(); ++index) {
            if (DolphinTabPage *page = tabWidget->tabPageAt(index); !known.contains(page)) {
                known.append(page);
                return page->primaryViewContainer()->url().adjusted(QUrl::StripTrailingSlash);
            }
        }
        return QUrl();
    };
    const QList<QPair<QString, QUrl>> expected = {
        {QStringLiteral("go_back"), m_dir->url()},
        {QStringLiteral("go_forward"), url(QStringLiteral("sub2"))},
        {QStringLiteral("go_up"), m_dir->url()},
        {QStringLiteral("go_home"), Dolphin::homeUrl().adjusted(QUrl::StripTrailingSlash)},
    };
    int tabs = 1;
    for (const auto &[name, target] : expected) {
        middleClick(m_right, name);
        ++tabs;
        QTRY_COMPARE(tabWidget->count(), tabs);
        QCOMPARE(urlOfNewTab(), target);
        // In the background; neither view navigated.
        QCOMPARE(tabWidget->currentIndex(), 0);
        QCOMPARE(m_right->url().fileName(), QStringLiteral("sub"));
        QCOMPARE(m_left->url(), m_dir->url());
    }
    // The first view has no history: nothing to open.
    middleClick(m_left, QStringLiteral("go_back"));
    QTest::qWait(100);
    QCOMPARE(tabWidget->count(), tabs);

    // An entry of the history menu.
    auto back = qobject_cast<KToolBarPopupAction *>(local(m_right, QStringLiteral("go_back")));
    Q_EMIT back->popupMenu()->aboutToShow();
    QCOMPARE(back->popupMenu()->actions().count(), 1);
    toolBar(m_right)->scope()->commands()->openInNewTab(back->popupMenu()->actions().first());
    ++tabs;
    QTRY_COMPARE(tabWidget->count(), tabs);
    QCOMPARE(urlOfNewTab(), m_dir->url());
    // Other actions open nothing.
    toolBar(m_right)->scope()->commands()->openInNewTab(local(m_right, QStringLiteral("view_redisplay")));
    QCOMPARE(tabWidget->count(), tabs);
}

void WhaleFileViewToolBarTest::testReloadAnnouncement()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    m_right->setUrl(url(QStringLiteral("sub2")));
    QTRY_COMPARE(m_right->url().fileName(), QStringLiteral("sub2"));
    QTRY_VERIFY(m_right->isAncestorOf(QApplication::focusWidget()));
    QTest::qWait(50);
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QSignalSpy refreshed(m_mainWindow.data(), &DolphinMainWindow::urlRefreshed);

    // As the window's Reload: the window announces the reload of its active view (its
    // terminal follows). Clicking the button makes the view the active one.
    clickLocal(m_right, QStringLiteral("view_redisplay"));
    QCOMPARE(refreshed.count(), 1);
    QCOMPARE(refreshed.first().first().toUrl().fileName(), QStringLiteral("sub2"));
    global(KStandardAction::name(KStandardAction::Redisplay))->trigger();
    QCOMPARE(refreshed.count(), 2);
    QCOMPARE(refreshed.last().first().toUrl().fileName(), QStringLiteral("sub2"));

    // Triggered for a view that is not the active one: reloaded, but not announced as the
    // active view's reload.
    QVERIFY(m_right->isActive());
    QSignalSpy loading(m_left->view(), &DolphinView::directoryLoadingStarted);
    local(m_left, QStringLiteral("view_redisplay"))->trigger();
    QTRY_COMPARE(loading.count(), 1);
    QCOMPARE(refreshed.count(), 2);
}

void WhaleFileViewToolBarTest::testStopState()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    const QString stop = QStringLiteral("stop");
    QTRY_VERIFY(!local(m_left, stop)->isEnabled());
    QTRY_VERIFY(!local(m_right, stop)->isEnabled());
    // While the one view loads.
    Q_EMIT m_right->view()->directoryLoadingStarted();
    QVERIFY(local(m_right, stop)->isEnabled());
    QVERIFY(!local(m_left, stop)->isEnabled());
    Q_EMIT m_left->view()->directoryLoadingStarted();
    Q_EMIT m_right->view()->directoryLoadingCompleted();
    QVERIFY(!local(m_right, stop)->isEnabled());
    QVERIFY(local(m_left, stop)->isEnabled());
    Q_EMIT m_left->view()->directoryLoadingCompleted();
    QVERIFY(!local(m_left, stop)->isEnabled());
}

void WhaleFileViewToolBarTest::testMenusArePopups()
{
    // A second window, created while the first one is active. Each window's views use that
    // window's services, and no menu of a view belongs to another window or is a plain
    // child widget (it would be drawn inside of the window, clipped).
    showSplit();
    if (QTest::currentTestFailed()) {
        return; // no window to test with (e.g. not activated under Wayland)
    }
    QVERIFY(QApplication::activeWindow());
    QScopedPointer<DolphinMainWindow> second(new DolphinMainWindow());
    second->openDirectories({url(QStringLiteral("sub"))}, false);
    second->resize(900, 600);
    second->show();
    QVERIFY(QTest::qWaitForWindowExposed(second.data()));
    DolphinViewContainer *other = second->activeViewContainer();
    QTRY_VERIFY(toolBar(other) && toolBar(other)->scope());
    QCOMPARE(toolBar(other)->scope()->viewActions()->services(), WhaleViewServices::of(second.data()));
    QVERIFY(WhaleViewServices::of(second.data()) != WhaleViewServices::of(m_mainWindow.data()));
    QVERIFY(WhaleToolbarManager::of(other) != WhaleToolbarManager::of(m_left));

    int menus = 0;
    for (DolphinViewContainer *container : {m_left, m_right, other}) {
        QList<QAction *> actions;
        const QStringList names = WhaleViewCommandScope::allowedActions();
        for (const QString &name : names) {
            actions.append(local(container, name));
        }
        const auto all = reachableActions(actions);
        for (QAction *action : all) {
            // Every action of the view belongs to objects of its own window.
            for (QObject *parent = action->parent(); parent; parent = parent->parent()) {
                if (parent->isWidgetType() && static_cast<QWidget *>(parent)->isWindow() && !qobject_cast<QMenu *>(parent)) {
                    QVERIFY2(parent == container->window(), qPrintable(action->objectName()));
                }
            }
            QMenu *menu = action->menu();
            if (!menu) {
                continue;
            }
            ++menus;
            QVERIFY2(menu->windowType() == Qt::Popup, qPrintable(action->objectName()));
            QVERIFY2(menu->isWindow(), qPrintable(action->objectName()));
            if (QWidget *parent = menu->parentWidget()) {
                QVERIFY2(parent->window() == container->window(), qPrintable(action->objectName()));
            }
        }
    }
    QVERIFY(menus >= 3 * 5); // view_settings, view_mode, sort, group_by, additional_info, go_back, go_forward …

    // The window's Back acts in its own window only.
    m_right->setUrl(url(QStringLiteral("sub2")));
    QTRY_COMPARE(m_right->url().fileName(), QStringLiteral("sub2"));
    local(other, QStringLiteral("go_up"))->trigger();
    QTRY_COMPARE(other->url().adjusted(QUrl::StripTrailingSlash), m_dir->url());
    QCOMPARE(m_right->url().fileName(), QStringLiteral("sub2"));
    QCOMPARE(m_left->url(), m_dir->url());

    QPointer<WhaleFileViewToolBar> otherBar = toolBar(other);
    second.reset();
    QVERIFY(!otherBar);
    QVERIFY(local(m_left, QStringLiteral("view_settings")));
}

QTEST_MAIN(WhaleFileViewToolBarTest)

#include "whalefileviewtoolbartest.moc"
