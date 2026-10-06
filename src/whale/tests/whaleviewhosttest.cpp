/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewhost.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/private/kitemlistroleeditor.h"
#include "panels/places/placespanel.h"
#include "panels/terminal/terminalpanel.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "views/dolphinviewactionhandler.h"
#include "whaleview.h"
#include "whaleviewplugin.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KHamburgerMenu>
#include <KIO/RenameFileDialog>
#include <KLocalizedString>
#include <KSelectAction>
#include <KUrlComboBox>
#include <KUrlNavigator>

#include <KToolBar>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace
{
const QString helloId = QStringLiteral("whale_hello");
const QString testListId = QStringLiteral("whale_testlist");
const QString recordingId = QStringLiteral("whale_test_recording");

/// A view that counts its lifecycle calls and shows a focusable line edit.
class RecordingView : public WhaleView
{
public:
    RecordingView(WhaleViewContext *context, int *activations, int *deactivations)
        : WhaleView(context)
        , m_edit(new QLineEdit)
        , m_activations(activations)
        , m_deactivations(deactivations)
    {
        m_edit->setObjectName(QStringLiteral("recordingViewWidget"));
    }

    ~RecordingView() override
    {
        delete m_edit;
    }

    QWidget *widget() override
    {
        return m_edit;
    }

    void activate() override
    {
        ++*m_activations;
    }

    void deactivate() override
    {
        ++*m_deactivations;
    }

private:
    QPointer<QLineEdit> m_edit;
    int *m_activations;
    int *m_deactivations;
};

class RecordingPlugin : public WhaleViewPlugin
{
public:
    WhaleView *createView(WhaleViewContext *context, QObject *parent) override
    {
        Q_UNUSED(parent)
        ++created;
        return new RecordingView(context, &activations, &deactivations);
    }

    int created = 0;
    int activations = 0;
    int deactivations = 0;
};

KItemListContainer *itemListContainer(DolphinView *view)
{
    return view->findChild<KItemListContainer *>(QString(), Qt::FindDirectChildrenOnly);
}

bool hasFocusInside(QWidget *widget)
{
    QWidget *focus = QApplication::focusWidget();
    return focus && (focus == widget || widget->isAncestorOf(focus));
}

void clickInto(QWidget *widget)
{
    // Scroll areas such as the test list get mouse events on their viewport.
    if (auto area = qobject_cast<QAbstractScrollArea *>(widget)) {
        widget = area->viewport();
    }
    QTest::mouseClick(widget, Qt::LeftButton, {}, widget->rect().center());
}
}

class WhaleViewHostTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSwitcherListsViewsById();
    void testSwitcherIsReachable();
    void testSwitchBetweenDolphinAndHello();
    void testLifecycle();
    void testShellKeepsDolphinView();
    void testFocusAfterSwitching_data();
    void testFocusAfterSwitching();
    void testShellFocusPathsReachPluginView();
    void testFocusAfterNavigation();
    void testHiddenDolphinViewNeverGetsFocus_data();
    void testHiddenDolphinViewNeverGetsFocus();
    void testSplitView_data();
    void testSplitView();
    void testRenameWhilePluginShown();
    void testRenameWhileDolphinViewShown();

private:
    DolphinViewContainer *showMainWindow(const QUrl &url);
    QAction *switcherAction(const QString &viewId) const;
    QWidget *pluginWidget(DolphinViewContainer *container) const;

    QScopedPointer<DolphinMainWindow> m_mainWindow;
    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;

    // Every focus change into a DolphinView whose host shows a plugin view.
    QStringList m_focusIntoHiddenDolphinView;
};

void WhaleViewHostTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QApplication::setApplicationDisplayName(QStringLiteral(WHALE_DISPLAY_NAME));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    KLocalizedString::setApplicationDomain({"whale"});

    // Only the plugins of this build tree (the Hello plugin).
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));

    GeneralSettings *settings = GeneralSettings::self();
    settings->setShowStatusBar(GeneralSettings::EnumShowStatusBar::FullWidth);
    settings->setRenameInline(true);
    settings->save();

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!now || !m_mainWindow) {
            return;
        }
        const auto containers = m_mainWindow->findChildren<DolphinViewContainer *>();
        for (DolphinViewContainer *container : containers) {
            DolphinView *view = container->view();
            if (!container->viewHost()->isDolphinViewShown() && (now == view || view->isAncestorOf(now))) {
                m_focusIntoHiddenDolphinView.append(QString::fromLatin1(now->metaObject()->className()));
            }
        }
    });
}

void WhaleViewHostTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFiles({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    m_dir->createDir(QStringLiteral("sub"));
    m_focusIntoHiddenDolphinView.clear();
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleViewHostTest::cleanup()
{
    QVERIFY2(m_focusIntoHiddenDolphinView.isEmpty(), qPrintable(m_focusIntoHiddenDolphinView.join(QStringLiteral(", "))));
    m_mainWindow.reset();
    m_dir.reset();
}

DolphinViewContainer *WhaleViewHostTest::showMainWindow(const QUrl &url)
{
    m_mainWindow->openDirectories({url}, false);
    m_mainWindow->resize(1600, 900);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 3;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QAction *WhaleViewHostTest::switcherAction(const QString &viewId) const
{
    auto switcher = qobject_cast<KSelectAction *>(m_mainWindow->actionCollection()->action(QStringLiteral("whale_view_switcher")));
    if (!switcher) {
        return nullptr;
    }
    const auto actions = switcher->actions();
    for (QAction *action : actions) {
        if (action->data().toString() == viewId) {
            return action;
        }
    }
    return nullptr;
}

QWidget *WhaleViewHostTest::pluginWidget(DolphinViewContainer *container) const
{
    WhaleViewHost *host = container->viewHost();
    return host->isDolphinViewShown() ? nullptr : host->currentView()->widget();
}

void WhaleViewHostTest::testSwitcherListsViewsById()
{
    QVERIFY(showMainWindow(m_dir->url()));
    QAction *switcher = m_mainWindow->actionCollection()->action(QStringLiteral("whale_view_switcher"));
    QVERIFY(switcher);
    QVERIFY(switcher->isVisible());
    QVERIFY(switcher->isEnabled());

    QAction *fileList = switcherAction(WhaleViewHost::dolphinViewId());
    QAction *hello = switcherAction(helloId);
    QVERIFY(fileList);
    QVERIFY(hello);
    QCOMPARE(hello->text(), QStringLiteral("Hello"));

    // Every view appears once, even if a plugin is found in several plugin directories.
    QStringList ids;
    const auto actions = qobject_cast<KSelectAction *>(switcher)->actions();
    for (QAction *action : actions) {
        ids.append(action->data().toString());
    }
    QCOMPARE(ids.removeDuplicates(), 0);
    QVERIFY(fileList->isChecked());
    QVERIFY(!hello->isChecked());
}

void WhaleViewHostTest::testSwitcherIsReachable()
{
    QVERIFY(showMainWindow(m_dir->url()));
    KActionCollection *ac = m_mainWindow->actionCollection();
    QAction *switcher = ac->action(QStringLiteral("whale_view_switcher"));

    // In the toolbar's view settings dropdown, right after the view modes.
    auto viewSettings = qobject_cast<KActionMenu *>(ac->action(QStringLiteral("view_settings")));
    QVERIFY(viewSettings);
    const auto entries = viewSettings->menu()->actions();
    QCOMPARE(entries.indexOf(switcher), entries.indexOf(ac->action(QStringLiteral("details"))) + 1);

    // In the hamburger menu when the view settings are not reachable from the toolbar.
    auto hamburger = qobject_cast<KHamburgerMenu *>(ac->action(KStandardAction::name(KStandardAction::HamburgerMenu)));
    QVERIFY(hamburger);
    m_mainWindow->toolBar()->hide();
    QVERIFY(QMetaObject::invokeMethod(m_mainWindow.data(), "updateHamburgerMenu"));
    QVERIFY(hamburger->menu()->actions().contains(switcher));
}

void WhaleViewHostTest::testSwitchBetweenDolphinAndHello()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url());
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    DolphinView *view = container->view();
    QWidget *originalFocusProxy = view->focusProxy();
    QVERIFY(originalFocusProxy);
    QSignalSpy viewChanged(host, &WhaleViewHost::currentViewChanged);

    // Dolphin → Hello
    switcherAction(helloId)->trigger();
    QCOMPARE(viewChanged.count(), 1);
    QCOMPARE(viewChanged.at(0).at(0).toString(), helloId);
    QCOMPARE(host->currentViewId(), helloId);
    QVERIFY(!host->isDolphinViewShown());
    QVERIFY(switcherAction(helloId)->isChecked());
    QVERIFY(!switcherAction(WhaleViewHost::dolphinViewId())->isChecked());

    QPointer<QWidget> helloWidget = pluginWidget(container);
    QVERIFY(helloWidget);
    QVERIFY(helloWidget->isVisible());
    QVERIFY(qobject_cast<QLabel *>(helloWidget));
    QVERIFY(qobject_cast<QLabel *>(helloWidget)->text().contains(m_dir->path()));
    QVERIFY(!view->isVisible());
    QVERIFY(view->isEnabled()); // stays the shell's focus target
    QVERIFY(!itemListContainer(view)->isEnabled());
    QCOMPARE(view->focusProxy(), helloWidget.data());

    // Hello → Dolphin
    switcherAction(WhaleViewHost::dolphinViewId())->trigger();
    QCOMPARE(viewChanged.count(), 2);
    QCOMPARE(host->currentViewId(), WhaleViewHost::dolphinViewId());
    QVERIFY(host->isDolphinViewShown());
    QVERIFY(switcherAction(WhaleViewHost::dolphinViewId())->isChecked());
    QVERIFY(view->isVisible());
    QVERIFY(itemListContainer(view)->isEnabled());
    QCOMPARE(view->focusProxy(), originalFocusProxy);
    QVERIFY(!helloWidget); // the plugin view and its widget are gone
}

void WhaleViewHostTest::testLifecycle()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url());
    QVERIFY(container);
    WhaleViewHost *host = container->viewHost();
    RecordingPlugin plugin;

    QVERIFY(host->showPluginView(recordingId, &plugin));
    QCOMPARE(plugin.created, 1);
    QCOMPARE(plugin.activations, 1);
    QCOMPARE(plugin.deactivations, 0);

    // Showing the same view again does nothing.
    QVERIFY(host->showPluginView(recordingId, &plugin));
    QCOMPARE(plugin.created, 1);

    // Plugin → other plugin: the old view is deactivated and destroyed, the DolphinView stays silenced.
    QPointer<QWidget> recordingWidget = pluginWidget(container);
    switcherAction(helloId)->trigger();
    QCOMPARE(host->currentViewId(), helloId);
    QCOMPARE(plugin.deactivations, 1);
    QVERIFY(!recordingWidget);
    QVERIFY(!itemListContainer(container->view())->isEnabled());
    QCOMPARE(container->view()->focusProxy(), pluginWidget(container));

    // Plugin → Dolphin restores the DolphinView completely.
    QVERIFY(host->showPluginView(recordingId, &plugin));
    QCOMPARE(plugin.activations, 2);
    host->showDolphinView();
    QCOMPARE(plugin.deactivations, 2);
    QVERIFY(itemListContainer(container->view())->isEnabled());
    QCOMPARE(container->view()->focusProxy(), itemListContainer(container->view()));
}

void WhaleViewHostTest::testShellKeepsDolphinView()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url());
    QVERIFY(container);
    DolphinView *view = container->view();
    switcherAction(helloId)->trigger();

    // view() is still the same DolphinView, and shell actions keep working on it.
    QCOMPARE(container->view(), view);
    QCOMPARE(m_mainWindow->activeViewContainer()->view(), view);

    m_mainWindow->actionCollection()->action(QStringLiteral("details"))->trigger();
    QCOMPARE(view->viewMode(), DolphinView::DetailsView);
    m_mainWindow->actionCollection()->action(QStringLiteral("icons"))->trigger();
    QCOMPARE(view->viewMode(), DolphinView::IconsView);

    m_dir->createFile(QStringLiteral(".hidden"));
    m_mainWindow->actionCollection()->action(QStringLiteral("show_hidden_files"))->trigger();
    QTRY_COMPARE(view->itemsCount(), 4);
    m_mainWindow->actionCollection()->action(QStringLiteral("show_hidden_files"))->trigger();
    QTRY_COMPARE(view->itemsCount(), 3);
}

void WhaleViewHostTest::testFocusAfterSwitching_data()
{
    QTest::addColumn<QString>("pluginId");
    QTest::newRow("hello") << helloId;
    QTest::newRow("test list") << testListId;
}

void WhaleViewHostTest::testFocusAfterSwitching()
{
    QFETCH(QString, pluginId);
    DolphinViewContainer *container = showMainWindow(m_dir->url());
    QVERIFY(container);
    DolphinView *view = container->view();
    view->setFocus();
    QTRY_VERIFY(hasFocusInside(view));

    // Dolphin → plugin while the DolphinView has the focus.
    switcherAction(pluginId)->trigger();
    QTRY_COMPARE(QApplication::focusWidget(), pluginWidget(container));
    QVERIFY(container->isActive());

    // Plugin → Dolphin while the plugin has the focus.
    switcherAction(WhaleViewHost::dolphinViewId())->trigger();
    QTRY_COMPARE(QApplication::focusWidget(), itemListContainer(view));
    QVERIFY(container->isActive());

    // Switching while the focus is elsewhere (location bar) still focuses the new view of the active area.
    m_mainWindow->actionCollection()->action(QStringLiteral("replace_location"))->trigger();
    QTRY_VERIFY(container->urlNavigator()->editor()->lineEdit()->hasFocus());
    switcherAction(pluginId)->trigger();
    QTRY_COMPARE(QApplication::focusWidget(), pluginWidget(container));
}

void WhaleViewHostTest::testShellFocusPathsReachPluginView()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url());
    QVERIFY(container);
    KActionCollection *ac = m_mainWindow->actionCollection();
    switcherAction(helloId)->trigger();
    QWidget *hello = pluginWidget(container);
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    auto focusAway = [this, container]() {
        QLineEdit *edit = container->urlNavigator()->editor()->lineEdit();
        container->urlNavigator()->setUrlEditable(true);
        edit->setFocus();
        return QTest::qWaitFor(
            [edit]() {
                return edit->hasFocus();
            },
            5000);
    };

    // 1. DolphinMainWindow::showEvent. A real hide()/show() cycle loses the focus completely on the
    // offscreen platform (also without any plugin), so the non-spontaneous show event is sent directly.
    QVERIFY(focusAway());
    QShowEvent showEvent;
    QCoreApplication::sendEvent(m_mainWindow.data(), &showEvent);
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    // 2. DolphinMainWindow::replaceLocation (pressed twice returns to the view)
    ac->action(QStringLiteral("replace_location"))->trigger();
    QTRY_VERIFY(container->urlNavigator()->editor()->lineEdit()->hasFocus());
    container->urlNavigator()->editor()->lineEdit()->selectAll();
    ac->action(QStringLiteral("replace_location"))->trigger();
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    // 3. DolphinMainWindow::togglePlacesPanelFocus
    auto placesPanel = m_mainWindow->findChild<PlacesPanel *>();
    QVERIFY(placesPanel);
    if (!placesPanel->isVisible()) {
        ac->action(QStringLiteral("show_places_panel"))->trigger();
    }
    QTRY_VERIFY(placesPanel->isVisible());
    placesPanel->setFocus();
    QTRY_VERIFY(placesPanel->hasFocus());
    ac->action(QStringLiteral("focus_places_panel"))->trigger();
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    // 4. DolphinMainWindow::slotPlacesPanelVisibilityChanged(false)
    placesPanel->setFocus();
    QTRY_VERIFY(placesPanel->hasFocus());
    ac->action(QStringLiteral("show_places_panel"))->trigger();
    QTRY_VERIFY(!placesPanel->isVisible());
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    // 5. DolphinMainWindow::slotPlaceActivated (also navigates while the plugin view is shown)
    QVERIFY(focusAway());
    const QUrl subUrl = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub"));
    QVERIFY(QMetaObject::invokeMethod(m_mainWindow.data(), "slotPlaceActivated", Q_ARG(QUrl, subUrl)));
    QTRY_COMPARE(container->url(), subUrl);
    QTRY_COMPARE(QApplication::focusWidget(), hello);
    QTRY_VERIFY(qobject_cast<QLabel *>(hello)->text().contains(QStringLiteral("/sub")));

    // 6. DolphinMainWindow::toggleTerminalPanelFocus and 7. slotTerminalPanelVisibilityChanged(false)
    auto terminalPanel = m_mainWindow->findChild<TerminalPanel *>();
    QVERIFY(terminalPanel);
    ac->action(QStringLiteral("show_terminal_panel"))->trigger();
    QTRY_VERIFY(terminalPanel->isVisible());
    terminalPanel->setFocus();
    QTRY_VERIFY(terminalPanel->terminalHasFocus());
    ac->action(QStringLiteral("focus_terminal_panel"))->trigger();
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    terminalPanel->setFocus();
    QTRY_VERIFY(terminalPanel->terminalHasFocus());
    ac->action(QStringLiteral("show_terminal_panel"))->trigger();
    QTRY_VERIFY(!terminalPanel->isVisible());
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    // Filter bar: closing it returns the focus to the view (DolphinViewContainer::closeFilterBar).
    ac->action(QStringLiteral("show_filter_bar"))->trigger();
    QTRY_VERIFY(!hasFocusInside(hello));
    container->setFilterBarVisible(false);
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    // 8. DolphinTabWidget::openNewActivatedTab: the new tab shows the file list and gets the focus.
    auto tabWidget = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    ac->action(QStringLiteral("new_tab"))->trigger();
    QTRY_COMPARE(tabWidget->count(), 2);
    DolphinViewContainer *newContainer = tabWidget->currentTabPage()->activeViewContainer();
    QVERIFY(newContainer != container);
    QVERIFY(newContainer->viewHost()->isDolphinViewShown());
    QTRY_VERIFY(hasFocusInside(newContainer->view()));
    QCOMPARE(container->viewHost()->currentViewId(), helloId); // the old tab keeps its view

    // Back to the first tab: its plugin view gets the focus again.
    tabWidget->setCurrentIndex(0);
    QTRY_COMPARE(QApplication::focusWidget(), hello);
    QVERIFY(switcherAction(helloId)->isChecked());
}

void WhaleViewHostTest::testFocusAfterNavigation()
{
    DolphinViewContainer *container = showMainWindow(m_dir->url());
    QVERIFY(container);
    switcherAction(helloId)->trigger();
    QWidget *hello = pluginWidget(container);

    // Navigating requests the focus for the view asynchronously (DolphinViewContainer::requestFocus).
    const QUrl subUrl = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub"));
    QSignalSpy loaded(container->view(), &DolphinView::directoryLoadingCompleted);
    container->setUrl(subUrl);
    QVERIFY(loaded.wait(5000));
    QTRY_COMPARE(QApplication::focusWidget(), hello);
    QCOMPARE(container->viewHost()->currentViewId(), helloId);

    // Back in the history: the hidden DolphinView follows, the plugin view stays.
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_COMPARE(container->url().adjusted(QUrl::StripTrailingSlash), m_dir->url().adjusted(QUrl::StripTrailingSlash));
    QTRY_COMPARE(QApplication::focusWidget(), hello);
    QTRY_COMPARE(container->view()->itemsCount(), 3);
}

void WhaleViewHostTest::testHiddenDolphinViewNeverGetsFocus_data()
{
    QTest::addColumn<QString>("pluginId");
    QTest::newRow("hello") << helloId;
    QTest::newRow("test list") << testListId;
}

void WhaleViewHostTest::testHiddenDolphinViewNeverGetsFocus()
{
    QFETCH(QString, pluginId);
    DolphinViewContainer *container = showMainWindow(m_dir->url());
    QVERIFY(container);
    DolphinView *view = container->view();
    switcherAction(pluginId)->trigger();
    QWidget *plugin = pluginWidget(container);
    QTRY_COMPARE(QApplication::focusWidget(), plugin);

    // What DolphinView::setActive() does internally: focus its item list container.
    itemListContainer(view)->setFocus();
    QCOMPARE(QApplication::focusWidget(), plugin);

    // What the shell does: focus the DolphinView. The focus proxy forwards it.
    view->setFocus();
    QCOMPARE(QApplication::focusWidget(), plugin);

    // Deactivating and activating the view area.
    container->setActive(false);
    container->setActive(true);
    QTRY_COMPARE(QApplication::focusWidget(), plugin);

    // Tab key focus chain: never into the hidden view. (Checked for every focus change in cleanup().)
    for (int i = 0; i < 15; ++i) {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    }
    for (int i = 0; i < 15; ++i) {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab);
    }
}

void WhaleViewHostTest::testSplitView_data()
{
    QTest::addColumn<QString>("pluginId");
    QTest::addColumn<bool>("pluginLeft");
    QTest::newRow("hello left") << helloId << true;
    QTest::newRow("hello right") << helloId << false;
    QTest::newRow("test list left") << testListId << true;
    QTest::newRow("test list right") << testListId << false;
}

void WhaleViewHostTest::testSplitView()
{
    QFETCH(QString, pluginId);
    QFETCH(bool, pluginLeft);

    QVERIFY(showMainWindow(m_dir->url()));
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    auto tabWidget = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
    DolphinTabPage *page = tabWidget->currentTabPage();
    DolphinViewContainer *left = page->primaryViewContainer();
    DolphinViewContainer *right = page->secondaryViewContainer();
    QVERIFY(left && right);
    QTRY_VERIFY(right->isActive());
    // The new half grows in with an animation.
    QTRY_VERIFY(qAbs(left->width() - right->width()) < 50);

    DolphinViewContainer *pluginSide = pluginLeft ? left : right;
    DolphinViewContainer *dolphinSide = pluginLeft ? right : left;

    // Activate the side that gets the plugin and switch it via the view switcher.
    pluginSide->setActive(true);
    QTRY_VERIFY(pluginSide->isActive());
    switcherAction(pluginId)->trigger();
    QCOMPARE(pluginSide->viewHost()->currentViewId(), pluginId);
    QCOMPARE(dolphinSide->viewHost()->currentViewId(), WhaleViewHost::dolphinViewId());
    QWidget *plugin = pluginWidget(pluginSide);
    QTRY_COMPARE(QApplication::focusWidget(), plugin);

    // Mouse into the file list on the other side: that side becomes active.
    clickInto(itemListContainer(dolphinSide->view())->viewport());
    QTRY_VERIFY(dolphinSide->isActive());
    QVERIFY(!pluginSide->isActive());
    QCOMPARE(page->activeViewContainer(), dolphinSide);
    QVERIFY(hasFocusInside(dolphinSide->view()));
    QVERIFY(switcherAction(WhaleViewHost::dolphinViewId())->isChecked()); // the switcher follows the active side

    // Mouse into the plugin view: the plugin side becomes active (FocusIn → activation).
    clickInto(plugin);
    QTRY_VERIFY(pluginSide->isActive());
    QVERIFY(!dolphinSide->isActive());
    QCOMPARE(page->activeViewContainer(), pluginSide);
    QCOMPARE(QApplication::focusWidget(), plugin);
    QVERIFY(switcherAction(pluginId)->isChecked());

    // Keyboard: "Focus Other View" (Ctrl+F3, DolphinTabWidget::focusInactiveSplitView).
    QAction *focusOtherView = m_mainWindow->actionCollection()->action(QStringLiteral("focus_inactive_split_view"));
    QVERIFY(focusOtherView);
    focusOtherView->trigger();
    QTRY_VERIFY(dolphinSide->isActive());
    QVERIFY(!pluginSide->isActive());
    QTRY_VERIFY(hasFocusInside(dolphinSide->view()));
    focusOtherView->trigger();
    QTRY_VERIFY(pluginSide->isActive());
    QVERIFY(!dolphinSide->isActive());
    QTRY_COMPARE(QApplication::focusWidget(), plugin);

    // The shell focuses the active view; the inactive plugin side must not take over.
    dolphinSide->setActive(true);
    QTRY_VERIFY(dolphinSide->isActive());
    pluginSide->view()->setFocus(); // the proxy forwards to the plugin widget, which activates its side like a click
    QTRY_VERIFY(pluginSide->isActive());
    QVERIFY(!dolphinSide->isActive());

    // Both sides can show different views independently.
    RecordingPlugin recording;
    QVERIFY(dolphinSide->viewHost()->showPluginView(recordingId, &recording));
    QCOMPARE(pluginSide->viewHost()->currentViewId(), pluginId);
    QCOMPARE(dolphinSide->viewHost()->currentViewId(), recordingId);
    clickInto(pluginWidget(dolphinSide));
    QTRY_VERIFY(dolphinSide->isActive());
    QVERIFY(!pluginSide->isActive());
    QCOMPARE(QApplication::focusWidget(), pluginWidget(dolphinSide));
    clickInto(plugin);
    QTRY_VERIFY(pluginSide->isActive());
    QVERIFY(!dolphinSide->isActive());

    // Back to the file list on the plugin side while it is active.
    switcherAction(WhaleViewHost::dolphinViewId())->trigger();
    QVERIFY(pluginSide->viewHost()->isDolphinViewShown());
    QTRY_VERIFY(hasFocusInside(pluginSide->view()));
    QVERIFY(pluginSide->isActive());
    QVERIFY(!dolphinSide->isActive());
    dolphinSide->viewHost()->showDolphinView();
}

void WhaleViewHostTest::testRenameWhilePluginShown()
{
    // One file only, so that "select all" selects exactly one item (the inline rename case).
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("only.txt"));
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowActive(m_mainWindow.data()));
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    DolphinView *view = container->view();
    QTRY_COMPARE(view->itemsCount(), 1);

    KActionCollection *ac = m_mainWindow->actionCollection();
    ac->action(KStandardAction::name(KStandardAction::SelectAll))->trigger();
    QTRY_COMPARE(view->selectedItemsCount(), 1);

    switcherAction(helloId)->trigger();
    QWidget *hello = pluginWidget(container);
    QTRY_COMPARE(QApplication::focusWidget(), hello);

    // The rename slot hands the action on synchronously (actionBeingHandled, then the inline
    // editor or the selection mode). Neither may happen while the DolphinView is hidden.
    auto actionHandler = m_mainWindow->findChild<DolphinViewActionHandler *>();
    QVERIFY(actionHandler);
    QSignalSpy handled(actionHandler, &DolphinViewActionHandler::actionBeingHandled);
    QSignalSpy selectionMode(actionHandler, &DolphinViewActionHandler::selectionModeChangeTriggered);

    QAction *rename = ac->action(KStandardAction::name(KStandardAction::RenameFile));
    rename->trigger();
    QTest::keyClick(hello, Qt::Key_F2);
    QCoreApplication::processEvents();

    QCOMPARE(handled.count(), 0);
    QCOMPARE(selectionMode.count(), 0);
    QVERIFY(!view->findChild<KItemListRoleEditor *>());
    QVERIFY(!view->findChild<KIO::RenameFileDialog *>());
    QVERIFY(!container->isSelectionModeEnabled());
    QCOMPARE(QApplication::focusWidget(), hello);

    // Without a selection the rename action would start the selection mode instead.
    view->clearSelection();
    QTRY_COMPARE(view->selectedItemsCount(), 0);
    rename->trigger();
    QCOMPARE(selectionMode.count(), 0);
    QVERIFY(!container->isSelectionModeEnabled());
}

void WhaleViewHostTest::testRenameWhileDolphinViewShown()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("only.txt"));
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->show();
    QVERIFY(QTest::qWaitForWindowActive(m_mainWindow.data()));
    DolphinView *view = m_mainWindow->activeViewContainer()->view();
    QTRY_COMPARE(view->itemsCount(), 1);

    // Switching to a plugin and back must not change the DolphinView's own rename behavior.
    switcherAction(helloId)->trigger();
    switcherAction(WhaleViewHost::dolphinViewId())->trigger();
    view->setFocus();

    KActionCollection *ac = m_mainWindow->actionCollection();
    ac->action(KStandardAction::name(KStandardAction::SelectAll))->trigger();
    QTRY_COMPARE(view->selectedItemsCount(), 1);
    ac->action(KStandardAction::name(KStandardAction::RenameFile))->trigger();

    QTRY_VERIFY(view->findChild<KItemListRoleEditor *>());
    KItemListRoleEditor *editor = view->findChild<KItemListRoleEditor *>();
    QTRY_VERIFY(editor->isVisible());
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(editor, Qt::Key_Escape);
}

QTEST_MAIN(WhaleViewHostTest)

#include "whaleviewhosttest.moc"
