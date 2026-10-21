/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalesplitlock.h"

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewcontainer.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistselectionmanager.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "views/viewproperties.h"
#include "whalecontentscope.h"
#include "whalefileviewtoolbar.h"
#include "whalesplitviewbutton.h"
#include "whaletoolbarsettings.h"
#include "whaletransientmessage.h"
#include "whaleview.h"
#include "whaleviewcommandscope.h"
#include "whaleviewhost.h"
#include "whaleviewplugin.h"
#include "whaleviewregistry.h"
#include "whaleviewservices.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KMessageWidget>

#include <QSignalSpy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>
#include <QVariantAnimation>

#include <algorithm>

namespace
{
const QString treemapId = QStringLiteral("whale_treemap");
// A plugin view without LinkedSelection.
const QString testListId = QStringLiteral("whale_testlist");

QVariantMap treemapState(QWidget *view)
{
    QVariantMap state;
    QMetaObject::invokeMethod(view, "treemapState", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, state));
    return state;
}

QUrl treemapSelection(QWidget *view)
{
    return QUrl(treemapState(view).value(QStringLiteral("selected")).toString());
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
KMessageWidget *shownMessage(DolphinViewContainer *container)
{
    const auto widgets = container->findChildren<KMessageWidget *>();
    for (KMessageWidget *widget : widgets) {
        if ((widget->isVisible() || widget->isShowAnimationRunning()) && !widget->isHideAnimationRunning()) {
            return widget;
        }
    }
    return nullptr;
}

QString messageText(DolphinViewContainer *container)
{
    KMessageWidget *widget = shownMessage(container);
    return widget ? widget->text() : QString();
}
}

class WhaleSplitLockTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testButtonPlacement();
    void testLockAndUnlock();
    void testLockability();
    void testNotLockable();
    void testReleaseAtUnlockableLocation();
    void testReleaseForPluginView();
    void testReleaseWhenSplitCloses();
    void testNotInSession();
    void testTransientMessage();
    void testLockTakesActiveLocation_data();
    void testLockTakesActiveLocation();
    void testViewsFollow();
    void testFollowBack();
    void testNoFollowWhenUnlocked();
    void testNoFollowToUnlockableLocation();
    void testFollowSymlinkTarget();
    void testViewModesKept_data();
    void testViewModesKept();
    void testNoDynamicViewWhileLocked();
    void testSelectionLimitedWhileLocked();
    void testMirrorSelection();
    void testMirrorOnLocking();
    void testNoMirrorFromInactiveView();
    void testMirrorIntoExpandableView();
    void testMirrorIntoIconsView();
    void testMirrorAfterFollowing();
    void testMirrorShowsWhatIsSelected();
    void testMirrorLeavesOutFilteredItems();
    void testLockWithTreemap();
    void testMirrorFromTreemap();
    void testMirrorIntoTreemap();

private:
    DolphinViewContainer *showMainWindow();
    void showSplit();
    /// Shows the plugin view @p pluginId in @p container; @return its widget once it is ready.
    QWidget *showPluginView(DolphinViewContainer *container, const QString &pluginId);
    /// Clicks the cell of @p target in the treemap @p view, then goes up to it (Up selects the folder).
    bool selectInTreemap(QWidget *view, const QUrl &target);
    DolphinTabPage *tabPage() const;
    WhaleSplitLock *lock() const;
    static WhaleFileViewToolBar *toolBar(DolphinViewContainer *container);
    QUrl url(const QString &relative) const;
    static QUrl normalized(const QUrl &url);
    static KItemListSelectionManager *selectionManager(DolphinViewContainer *container);
    static KFileItemModel *model(DolphinViewContainer *container);
    void select(DolphinViewContainer *container, const QStringList &relativePaths, const QString &current);
    QStringList selected(DolphinViewContainer *container) const;
    QString current(DolphinViewContainer *container) const;
    QString relative(const QUrl &url) const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    DolphinViewContainer *m_left = nullptr;
    DolphinViewContainer *m_right = nullptr;
};

void WhaleSplitLockTest::initTestCase()
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
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleSplitLockTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"));
    m_dir->createFile(QStringLiteral("sub/b.txt"));
    m_dir->createFile(QStringLiteral("sub/deeper/c.txt"));
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleSplitLockTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
    m_left = m_right = nullptr;
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    GeneralSettings::self()->setFollowSymlinksToTarget(false);
    GeneralSettings::self()->setGlobalViewProps(true);
    GeneralSettings::self()->setDynamicView(false);
}

DolphinViewContainer *WhaleSplitLockTest::showMainWindow()
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1200, 700);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 2;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

void WhaleSplitLockTest::showSplit()
{
    QVERIFY(showMainWindow());
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    m_left = tabPage()->primaryViewContainer();
    m_right = tabPage()->secondaryViewContainer();
    QVERIFY(m_left && m_right);
    QTRY_COMPARE(m_right->view()->itemsCount(), 2);
    QTRY_VERIFY(!splitAnimating(tabPage()));
    QTRY_VERIFY(toolBar(m_right)->scope());
    QTRY_VERIFY(lock()->button()->isVisible());
}

DolphinTabPage *WhaleSplitLockTest::tabPage() const
{
    return m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
}

WhaleSplitLock *WhaleSplitLockTest::lock() const
{
    return WhaleSplitLock::of(tabPage());
}

WhaleFileViewToolBar *WhaleSplitLockTest::toolBar(DolphinViewContainer *container)
{
    return container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly);
}

QUrl WhaleSplitLockTest::url(const QString &relative) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + relative);
}

QUrl WhaleSplitLockTest::normalized(const QUrl &url)
{
    return url.adjusted(QUrl::StripTrailingSlash);
}

KItemListSelectionManager *WhaleSplitLockTest::selectionManager(DolphinViewContainer *container)
{
    return container->view()->findChild<KItemListContainer *>()->controller()->selectionManager();
}

KFileItemModel *WhaleSplitLockTest::model(DolphinViewContainer *container)
{
    return qobject_cast<KFileItemModel *>(container->view()->findChild<KItemListContainer *>()->controller()->model());
}

void WhaleSplitLockTest::select(DolphinViewContainer *container, const QStringList &relativePaths, const QString &current)
{
    // As the user does it: in the active view.
    container->setActive(true);
    QTRY_VERIFY(container->isActive());
    KItemSet items;
    for (const QString &path : relativePaths) {
        const int index = model(container)->index(url(path));
        QVERIFY2(index >= 0, qPrintable(path));
        items.insert(index);
    }
    // As a click does: the anchored selection ends before the current item changes.
    KItemListSelectionManager *manager = selectionManager(container);
    const int currentIndex = model(container)->index(url(current));
    manager->endAnchoredSelection();
    manager->setCurrentItem(currentIndex);
    manager->setSelectedItems(items);
    manager->beginAnchoredSelection(currentIndex);
}

QString WhaleSplitLockTest::relative(const QUrl &url) const
{
    return url.path().mid(m_dir->path().length() + 1);
}

QStringList WhaleSplitLockTest::selected(DolphinViewContainer *container) const
{
    QStringList result;
    const auto items = container->view()->selectedItems();
    for (const KFileItem &item : items) {
        result.append(relative(item.url()));
    }
    result.sort();
    return result;
}

QString WhaleSplitLockTest::current(DolphinViewContainer *container) const
{
    return relative(container->view()->currentItemUrl());
}

void WhaleSplitLockTest::testButtonPlacement()
{
    // Only with a split view: at the start of the File View Toolbar of the right view, outside
    // of the layout, the split button at its end.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QTRY_VERIFY(toolBar(container)->scope());
    QVERIFY(lock());
    QToolButton *button = lock()->button();
    QVERIFY(!button->isVisible());
    QVERIFY(!toolBar(container)->leadingWidget());

    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    m_right = tabPage()->secondaryViewContainer();
    QVERIFY(m_right);
    WhaleFileViewToolBar *bar = toolBar(m_right);
    QTRY_VERIFY(bar->scope());
    QTRY_VERIFY(button->isVisible());
    QCOMPARE(button->parentWidget(), bar);
    QCOMPARE(bar->leadingWidget(), button);
    QCOMPARE(bar->trailingWidget(), WhaleSplitViewButton::buttonOf(tabPage()));
    QVERIFY(!toolBar(container)->leadingWidget());
    QVERIFY(button->isEnabled());
    QVERIFY(button->isCheckable());
    QVERIFY(!button->isChecked());
    // Before all buttons of the layout, over a placeholder that is not part of the layout.
    QWidget *goBack = nullptr;
    QTRY_VERIFY((goBack = bar->widgetForAction(bar->scope()->action(QStringLiteral("go_back")))) && goBack->isVisible());
    QVERIFY(button->geometry().left() >= 0);
    int shown = 0;
    for (QAction *action : bar->actions()) {
        QWidget *widget = bar->widgetForAction(action);
        if (!widget || !widget->isVisible()) {
            continue;
        }
        if (WhaleFileViewToolBar::isLeadingPlaceholder(action)) {
            QCOMPARE(widget->geometry().x(), button->geometry().x());
            continue;
        }
        ++shown;
        QVERIFY2(button->geometry().right() < widget->geometry().left(), qPrintable(action->objectName()));
    }
    QVERIFY(shown >= 4);
    // A middle click on Back is still Back's (open in a new tab), not the lock's.
    QCOMPARE(bar->actionAt(goBack->geometry().center()), bar->scope()->action(QStringLiteral("go_back")));
    QCOMPARE(bar->childAt(goBack->geometry().center()), goBack);

    // Hidden with the File View Toolbars.
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    QVERIFY(!button->isVisible());
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    QTRY_VERIFY(button->isVisible());

    // Gone with the right view.
    QPointer<DolphinViewContainer> right = m_right;
    m_mainWindow->closeRightSplitView();
    QTRY_VERIFY(!right);
    QTRY_VERIFY(!button->isVisible());
    QVERIFY(!toolBar(container)->leadingWidget());

    // A new tab has a lock of its own.
    m_mainWindow->actionCollection()->action(QStringLiteral("new_tab"))->trigger();
    QTRY_VERIFY(lock() && lock()->button() != button);
}

void WhaleSplitLockTest::testLockAndUnlock()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QToolButton *button = lock()->button();
    QSignalSpy spy(lock(), &WhaleSplitLock::lockedChanged);
    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(lock()->isLocked());
    QVERIFY(button->isChecked());
    QCOMPARE(button->icon().name(), QStringLiteral("link"));
    QCOMPARE(button->toolTip(), QStringLiteral("Unlink Views"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(!lock()->isLocked());
    QVERIFY(!button->isChecked());
    QCOMPARE(button->icon().name(), QStringLiteral("remove-link"));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
    // No message for what the user did.
    QVERIFY(messageText(m_left).isEmpty());
    QVERIFY(messageText(m_right).isEmpty());
}

void WhaleSplitLockTest::testLockability()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QVERIFY(WhaleSplitLock::lockability(m_right).lockable);
    QVERIFY(!WhaleSplitLock::lockability(nullptr).lockable);

    m_right->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_COMPARE(m_right->url(), QUrl(QStringLiteral("trash:/")));
    QCOMPARE(WhaleSplitLock::lockability(m_right).lockable, false);
    QCOMPARE(WhaleSplitLock::lockability(m_right).reason, QStringLiteral("Only local folders can be linked."));

    m_right->setUrl(QUrl(QStringLiteral("filenamesearch:?search=a&url=") + QString::fromUtf8(QUrl::toPercentEncoding(m_dir->url().toString()))));
    QTRY_VERIFY(isSearchUrl(m_right->url()));
    QCOMPARE(WhaleSplitLock::lockability(m_right).reason, QStringLiteral("Search results cannot be linked."));

    m_right->setUrl(m_dir->url());
    QTRY_COMPARE(m_right->url(), m_dir->url());
    QVERIFY(WhaleSplitLock::lockability(m_right).lockable);
    QVERIFY(showPluginView(m_right, treemapId));
    QVERIFY(WhaleSplitLock::lockability(m_right).lockable);
    QVERIFY(showPluginView(m_right, testListId));
    QCOMPARE(WhaleSplitLock::lockability(m_right).reason, QStringLiteral("This view cannot be linked."));
}

void WhaleSplitLockTest::testNotLockable()
{
    // A view that cannot be linked: the button is disabled and says why; locking is refused.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QToolButton *button = lock()->button();
    m_left->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_VERIFY(!button->isEnabled());
    QCOMPARE(button->toolTip(), QStringLiteral("The views cannot be linked. Only local folders can be linked."));
    lock()->setLocked(true);
    QVERIFY(!lock()->isLocked());
    QVERIFY(!button->isChecked());

    m_left->setUrl(m_dir->url());
    QTRY_VERIFY(button->isEnabled());
    lock()->setLocked(true);
    QVERIFY(lock()->isLocked());
}

void WhaleSplitLockTest::testReleaseAtUnlockableLocation()
{
    // E3: a locked view goes where it cannot be linked: released, and that view says why. Back
    // at a local folder, it stays released.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    lock()->setLocked(true);
    QVERIFY(lock()->isLocked());
    QSignalSpy spy(lock(), &WhaleSplitLock::lockedChanged);
    m_right->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_VERIFY(!lock()->isLocked());
    QCOMPARE(spy.count(), 1);
    QVERIFY(!lock()->button()->isChecked());
    QTRY_COMPARE(messageText(m_right), QStringLiteral("The views are no longer linked. Only local folders can be linked."));
    QCOMPARE(shownMessage(m_right)->messageType(), KMessageWidget::Information);
    QVERIFY(messageText(m_left).isEmpty());

    m_right->setUrl(m_dir->url());
    QTRY_VERIFY(lock()->button()->isEnabled());
    QVERIFY(!lock()->isLocked());
}

void WhaleSplitLockTest::testReleaseForPluginView()
{
    // A plugin view that cannot show the selection of the other view (no LinkedSelection)
    // releases the lock.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    lock()->setLocked(true);
    QVERIFY(showPluginView(m_left, testListId));
    QTRY_VERIFY(!lock()->isLocked());
    QTRY_COMPARE(messageText(m_left), QStringLiteral("The views are no longer linked. This view cannot be linked."));
    QVERIFY(!lock()->button()->isEnabled());

    m_left->viewHost()->showDolphinView();
    QTRY_VERIFY(lock()->button()->isEnabled());
    QVERIFY(!lock()->isLocked());
}

void WhaleSplitLockTest::testReleaseWhenSplitCloses()
{
    // Closing the split view releases the lock without a message; a new split view starts
    // unlocked.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    lock()->setLocked(true);
    QSignalSpy spy(lock(), &WhaleSplitLock::lockedChanged);
    QPointer<DolphinViewContainer> right = m_right;
    m_mainWindow->closeRightSplitView();
    QTRY_VERIFY(!right);
    QTRY_VERIFY(!lock()->isLocked());
    QCOMPARE(spy.count(), 1);
    QVERIFY(messageText(m_left).isEmpty());

    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    QTRY_VERIFY(tabPage()->secondaryViewContainer());
    QTRY_VERIFY(lock()->button()->isVisible());
    QVERIFY(!lock()->isLocked());
    QVERIFY(!lock()->button()->isChecked());
}

void WhaleSplitLockTest::testNotInSession()
{
    // E4: the state of a tab does not contain the lock.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    const QByteArray unlocked = tabPage()->saveState();
    lock()->setLocked(true);
    QCOMPARE(tabPage()->saveState(), unlocked);

    // Restored into a new tab: unlocked.
    m_mainWindow->actionCollection()->action(QStringLiteral("new_tab"))->trigger();
    QTRY_VERIFY(lock() && !lock()->button()->isVisible());
    tabPage()->restoreState(unlocked);
    QTRY_VERIFY(tabPage()->splitViewEnabled() && tabPage()->secondaryViewContainer());
    QTRY_VERIFY(lock()->button()->isVisible());
    QVERIFY(!lock()->isLocked());
}

void WhaleSplitLockTest::testTransientMessage()
{
    // A8: information that hides itself; the same message again restarts the time. It floats
    // above the view, so nothing moves, and lets the mouse through; the message bar of the
    // view area stays as it is.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    const QRect viewGeometry = container->viewHost()->geometry();
    WhaleTransientMessage::showIn(container, QStringLiteral("first"));
    WhaleTransientMessage *message = WhaleTransientMessage::of(container);
    QVERIFY(message);
    QVERIFY(message->isVisible());
    QCOMPARE(message->text(), QStringLiteral("first"));
    QCOMPARE(message->messageType(), KMessageWidget::Information);
    QVERIFY(message->testAttribute(Qt::WA_TransparentForMouseEvents));
    const QRect area = container->viewHost()->geometry();
    QVERIFY(area.contains(message->geometry()));
    QVERIFY(message->geometry().top() > area.center().y()); // at the bottom
    QTest::qWait(100);
    QCOMPARE(container->viewHost()->geometry(), viewGeometry);
    QTRY_VERIFY_WITH_TIMEOUT(!message->isVisible(), 8000);

    WhaleTransientMessage::showIn(container, QStringLiteral("again"));
    QTest::qWait(3000);
    WhaleTransientMessage::showIn(container, QStringLiteral("again"));
    QTest::qWait(2000);
    QVERIFY(message->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!message->isVisible(), 8000);

    // Independent of the message bar.
    container->showMessage(QStringLiteral("error"), KMessageWidget::Error);
    WhaleTransientMessage::showIn(container, QStringLiteral("transient"));
    QTRY_VERIFY_WITH_TIMEOUT(!message->isVisible(), 8000);
    QCOMPARE(messageText(container), QStringLiteral("error"));
}

void WhaleSplitLockTest::testLockTakesActiveLocation_data()
{
    QTest::addColumn<bool>("rightActive");
    QTest::newRow("left active") << false;
    QTest::newRow("right active") << true;
}

void WhaleSplitLockTest::testLockTakesActiveLocation()
{
    // E1: on locking, the other view goes to the location of the active one; the active view
    // stays active and where it is.
    QFETCH(bool, rightActive);
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_right->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub")));
    DolphinViewContainer *active = rightActive ? m_right : m_left;
    DolphinViewContainer *other = rightActive ? m_left : m_right;
    const QUrl target = normalized(active->url());
    active->setActive(true);
    QTRY_VERIFY(active->isActive());

    lock()->setLocked(true);
    QTRY_COMPARE(normalized(other->url()), target);
    QCOMPARE(normalized(active->url()), target);
    QVERIFY(active->isActive());
    QVERIFY(!other->isActive());
    QVERIFY(lock()->isLocked());
}

void WhaleSplitLockTest::testViewsFollow()
{
    // Each view follows where the other one goes, and stays the inactive one.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->setActive(true);
    lock()->setLocked(true);

    m_left->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub")));
    QTRY_COMPARE(m_right->view()->itemsCount(), 2);
    QVERIFY(m_left->isActive());
    QVERIFY(!m_right->isActive());

    m_right->setActive(true);
    QTRY_VERIFY(m_right->isActive());
    m_right->setUrl(url(QStringLiteral("sub/deeper")));
    QTRY_COMPARE(normalized(m_left->url()), url(QStringLiteral("sub/deeper")));
    QVERIFY(m_right->isActive());
    QVERIFY(!m_left->isActive());

    // Settles: no view goes on after the other.
    QTest::qWait(200);
    QCOMPARE(normalized(m_left->url()), url(QStringLiteral("sub/deeper")));
    QCOMPARE(normalized(m_right->url()), url(QStringLiteral("sub/deeper")));
    QVERIFY(lock()->isLocked());
}

void WhaleSplitLockTest::testFollowBack()
{
    // Back and Up in one view: the other one follows to that location (as a new entry in its
    // own history).
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->setActive(true);
    lock()->setLocked(true);
    m_left->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub")));
    m_left->setUrl(url(QStringLiteral("sub/deeper")));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub/deeper")));

    QTRY_VERIFY(toolBar(m_left)->scope());
    toolBar(m_left)->scope()->action(QStringLiteral("go_back"))->trigger();
    QTRY_COMPARE(normalized(m_left->url()), url(QStringLiteral("sub")));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub")));

    toolBar(m_right)->scope()->action(QStringLiteral("go_up"))->trigger();
    QTRY_COMPARE(normalized(m_right->url()), normalized(m_dir->url()));
    QTRY_COMPARE(normalized(m_left->url()), normalized(m_dir->url()));
    QVERIFY(lock()->isLocked());
}

void WhaleSplitLockTest::testNoFollowWhenUnlocked()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(normalized(m_left->url()), url(QStringLiteral("sub")));
    QTest::qWait(200);
    QCOMPARE(normalized(m_right->url()), normalized(m_dir->url()));

    // Nor after releasing.
    lock()->setLocked(true);
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub")));
    lock()->setLocked(false);
    m_left->setUrl(url(QStringLiteral("sub/deeper")));
    QTRY_COMPARE(normalized(m_left->url()), url(QStringLiteral("sub/deeper")));
    QTest::qWait(200);
    QCOMPARE(normalized(m_right->url()), url(QStringLiteral("sub")));
}

void WhaleSplitLockTest::testNoFollowToUnlockableLocation()
{
    // E3: the other view stays where it is; the lock is released.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    lock()->setLocked(true);
    m_left->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_VERIFY(!lock()->isLocked());
    QTest::qWait(200);
    QCOMPARE(normalized(m_right->url()), normalized(m_dir->url()));
}

void WhaleSplitLockTest::testFollowSymlinkTarget()
{
    // "Always follow symbolic links to their target": both views end at the target, the
    // following view stays inactive.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QVERIFY(QFile::link(m_dir->path() + QStringLiteral("/sub/deeper"), m_dir->path() + QStringLiteral("/link")));
    GeneralSettings::self()->setFollowSymlinksToTarget(true);
    m_left->setActive(true);
    lock()->setLocked(true);
    m_left->setUrl(url(QStringLiteral("link")));
    QTRY_COMPARE(normalized(m_left->url()), url(QStringLiteral("sub/deeper")));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub/deeper")));
    QTest::qWait(200);
    QCOMPARE(normalized(m_right->url()), url(QStringLiteral("sub/deeper")));
    QVERIFY(m_left->isActive());
    QVERIFY(!m_right->isActive());
}

void WhaleSplitLockTest::testViewModesKept_data()
{
    QTest::addColumn<bool>("globalViewProps");
    QTest::newRow("one style for all folders") << true;
    QTest::newRow("a style per folder") << false;
}

void WhaleSplitLockTest::testViewModesKept()
{
    // Both views show the same folder, each in its own view mode, whichever view goes on.
    QFETCH(bool, globalViewProps);
    GeneralSettings::self()->setGlobalViewProps(globalViewProps);
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->view()->setViewMode(DolphinView::DetailsView);
    m_right->view()->setViewMode(DolphinView::IconsView);
    m_left->setActive(true);
    lock()->setLocked(true);

    m_left->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub")));
    QCOMPARE(m_left->view()->viewMode(), DolphinView::DetailsView);
    QCOMPARE(m_right->view()->viewMode(), DolphinView::IconsView);

    m_right->setActive(true);
    QTRY_VERIFY(m_right->isActive());
    m_right->setUrl(url(QStringLiteral("sub/deeper")));
    QTRY_COMPARE(normalized(m_left->url()), url(QStringLiteral("sub/deeper")));
    QCOMPARE(m_left->view()->viewMode(), DolphinView::DetailsView);
    QCOMPARE(m_right->view()->viewMode(), DolphinView::IconsView);

    // Released, a view takes the view mode of the folder again.
    lock()->setLocked(false);
    {
        ViewProperties props(url(QStringLiteral("sub")));
        props.setViewMode(DolphinView::IconsView);
    }
    m_left->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(normalized(m_left->url()), url(QStringLiteral("sub")));
    QCOMPARE(m_left->view()->viewMode(), DolphinView::IconsView);
}

void WhaleSplitLockTest::testNoDynamicViewWhileLocked()
{
    // The dynamic view (Icons for a folder of images) does not switch a linked view either.
    GeneralSettings::self()->setDynamicView(true);
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::red);
    m_dir->createDir(QStringLiteral("images"));
    for (const QString &name : {QStringLiteral("1.png"), QStringLiteral("2.png")}) {
        QVERIFY(image.save(m_dir->path() + QStringLiteral("/images/") + name));
    }
    m_dir->createDir(QStringLiteral("more images"));
    QVERIFY(image.save(m_dir->path() + QStringLiteral("/more images/3.png")));
    m_left->view()->setViewMode(DolphinView::DetailsView);
    m_right->view()->setViewMode(DolphinView::DetailsView);
    m_left->setActive(true);
    lock()->setLocked(true);

    m_left->setUrl(url(QStringLiteral("images")));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("images")));
    QTRY_COMPARE(m_right->view()->itemsCount(), 2);
    QTest::qWait(200);
    QCOMPARE(m_left->view()->viewMode(), DolphinView::DetailsView);
    QCOMPARE(m_right->view()->viewMode(), DolphinView::DetailsView);

    // Released, it does.
    lock()->setLocked(false);
    m_left->setUrl(url(QStringLiteral("more images")));
    QTRY_COMPARE(m_left->view()->viewMode(), DolphinView::IconsView);
}

void WhaleSplitLockTest::testSelectionLimitedWhileLocked()
{
    // A1: only while locked, in both views; the view that refuses says why (A8).
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QVERIFY(!m_left->view()->isSelectionLimitedToOneFolder());
    QVERIFY(!m_right->view()->isSelectionLimitedToOneFolder());
    lock()->setLocked(true);
    QVERIFY(m_left->view()->isSelectionLimitedToOneFolder());
    QVERIFY(m_right->view()->isSelectionLimitedToOneFolder());

    KItemListSelectionManager *selection = m_right->view()->findChild<KItemListContainer *>()->controller()->selectionManager();
    selection->setSelected(0);
    QVERIFY(selection->isGroupLimited());
    Q_EMIT selection->selectionRestricted();
    QTRY_COMPARE(messageText(m_right), QStringLiteral("While the views are linked, only items of one folder can be selected."));
    QVERIFY(messageText(m_left).isEmpty());

    // In the flat view, the message says that the rule is about folders it does not show.
    m_right->viewHost()->selectContentScope(WhaleContentScope::RecursiveFiles);
    QTRY_VERIFY(m_right->viewHost()->isRecursiveContentShown());
    QVERIFY(lock()->isLocked());
    Q_EMIT selection->selectionRestricted();
    QTRY_COMPARE(messageText(m_right),
                 QStringLiteral("While the views are linked, only items of one folder can be selected. This also applies to the flat view, which does "
                                "not show the folders."));
    m_right->viewHost()->selectContentScope(WhaleContentScope::CurrentFolder);

    lock()->setLocked(false);
    QVERIFY(!m_left->view()->isSelectionLimitedToOneFolder());
    QVERIFY(!m_right->view()->isSelectionLimitedToOneFolder());

    // Released by the split view closing: the remaining view is not limited.
    lock()->setLocked(true);
    QPointer<DolphinViewContainer> right = m_right;
    m_mainWindow->closeRightSplitView();
    QTRY_VERIFY(!right);
    QTRY_VERIFY(!lock()->isLocked());
    QTRY_VERIFY(!m_left->view()->isSelectionLimitedToOneFolder());
}

void WhaleSplitLockTest::testMirrorSelection()
{
    // LK4: the inactive view shows the selection and the current item of the active one.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->setActive(true);
    lock()->setLocked(true);
    select(m_left, {QStringLiteral("a.txt")}, QStringLiteral("a.txt"));
    QCOMPARE(selected(m_right), QStringList{QStringLiteral("a.txt")});
    QCOMPARE(current(m_right), QStringLiteral("a.txt"));
    QVERIFY(m_left->isActive());

    // The other way round: the user selects in the right view.
    select(m_right, {QStringLiteral("sub")}, QStringLiteral("sub"));
    QCOMPARE(selected(m_left), QStringList{QStringLiteral("sub")});
    QCOMPARE(current(m_left), QStringLiteral("sub"));
    QCOMPARE(selected(m_right), QStringList{QStringLiteral("sub")});

    // Nothing selected.
    m_right->view()->clearSelection();
    QVERIFY(selected(m_left).isEmpty());

    // Unlocked: no more.
    lock()->setLocked(false);
    select(m_right, {QStringLiteral("a.txt")}, QStringLiteral("a.txt"));
    QVERIFY(selected(m_left).isEmpty());
}

void WhaleSplitLockTest::testMirrorOnLocking()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    select(m_right, {QStringLiteral("a.txt")}, QStringLiteral("a.txt"));
    QVERIFY(selected(m_left).isEmpty());
    lock()->setLocked(true);
    QCOMPARE(selected(m_left), QStringList{QStringLiteral("a.txt")});
    QCOMPARE(current(m_left), QStringLiteral("a.txt"));
}

void WhaleSplitLockTest::testNoMirrorFromInactiveView()
{
    // What changes in the inactive view (e.g. while it lists its location) never reaches the
    // active one.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->setActive(true);
    lock()->setLocked(true);
    select(m_left, {QStringLiteral("a.txt")}, QStringLiteral("a.txt"));
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    selectionManager(m_right)->clearSelection();
    selectionManager(m_right)->setSelected(model(m_right)->index(url(QStringLiteral("sub"))));
    QCOMPARE(selected(m_left), QStringList{QStringLiteral("a.txt")});
}

void WhaleSplitLockTest::testMirrorIntoExpandableView()
{
    // An item of a subfolder: the details view expands its branch and selects it.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->view()->setViewMode(DolphinView::DetailsView);
    m_right->view()->setViewMode(DolphinView::DetailsView);
    QVERIFY(m_left->view()->itemsExpandable());
    QVERIFY(m_right->view()->itemsExpandable());
    m_left->setActive(true);
    lock()->setLocked(true);
    QVERIFY(model(m_left)->setExpanded(model(m_left)->index(url(QStringLiteral("sub"))), true));
    QTRY_VERIFY(model(m_left)->index(url(QStringLiteral("sub/deeper"))) >= 0);
    QVERIFY(model(m_left)->setExpanded(model(m_left)->index(url(QStringLiteral("sub/deeper"))), true));
    QTRY_VERIFY(model(m_left)->index(url(QStringLiteral("sub/deeper/c.txt"))) >= 0);

    select(m_left, {QStringLiteral("sub/deeper/c.txt")}, QStringLiteral("sub/deeper/c.txt"));
    QTRY_COMPARE(selected(m_right), QStringList{QStringLiteral("sub/deeper/c.txt")});
    QTRY_COMPARE(current(m_right), QStringLiteral("sub/deeper/c.txt"));
    QVERIFY(m_left->isActive());
    QCOMPARE(selected(m_left), QStringList{QStringLiteral("sub/deeper/c.txt")});
}

void WhaleSplitLockTest::testMirrorIntoIconsView()
{
    // An item of a subfolder in a view that cannot expand: its top-level folder becomes the
    // current item, nothing is selected (so nothing acts on it by surprise).
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->view()->setViewMode(DolphinView::DetailsView);
    m_right->view()->setViewMode(DolphinView::IconsView);
    QVERIFY(!m_right->view()->itemsExpandable());
    m_left->setActive(true);
    lock()->setLocked(true);
    QVERIFY(model(m_left)->setExpanded(model(m_left)->index(url(QStringLiteral("sub"))), true));
    QTRY_VERIFY(model(m_left)->index(url(QStringLiteral("sub/b.txt"))) >= 0);

    select(m_left, {QStringLiteral("sub/b.txt")}, QStringLiteral("sub/b.txt"));
    QTRY_COMPARE(current(m_right), QStringLiteral("sub"));
    QVERIFY(selected(m_right).isEmpty());
    // The left view keeps its selection.
    QCOMPARE(selected(m_left), QStringList{QStringLiteral("sub/b.txt")});
}

void WhaleSplitLockTest::testMirrorShowsWhatIsSelected()
{
    // The other view draws what its selection manager reports: an anchored range of before
    // (here from the top-level folder of a deep item) must not extend to the next current item.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_right->view()->setViewMode(DolphinView::IconsView);
    m_left->setActive(true);
    DolphinView *view = m_right->view();
    view->showLinkedSelection({url(QStringLiteral("sub/b.txt"))}, url(QStringLiteral("sub/b.txt")));
    QTRY_COMPARE(current(m_right), QStringLiteral("sub"));
    QVERIFY(selected(m_right).isEmpty());

    QSignalSpy spy(selectionManager(m_right), &KItemListSelectionManager::selectionChanged);
    view->showLinkedSelection({url(QStringLiteral("a.txt"))}, url(QStringLiteral("a.txt")));
    QCOMPARE(selected(m_right), QStringList{QStringLiteral("a.txt")});
    QVERIFY(!spy.isEmpty());
    QCOMPARE(spy.constLast().at(0).value<KItemSet>(), selectionManager(m_right)->selectedItems());
}

void WhaleSplitLockTest::testMirrorAfterFollowing()
{
    // The following view lists its new location: it shows the selection once the items come.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->setActive(true);
    lock()->setLocked(true);
    m_left->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(m_left->view()->itemsCount(), 2);
    select(m_left, {QStringLiteral("sub/b.txt")}, QStringLiteral("sub/b.txt"));
    QTRY_COMPARE(normalized(m_right->url()), url(QStringLiteral("sub")));
    QTRY_COMPARE(selected(m_right), QStringList{QStringLiteral("sub/b.txt")});
    QCOMPARE(selected(m_left), QStringList{QStringLiteral("sub/b.txt")});

    // Up: Dolphin makes the folder the view came from the current item; the other view too.
    QTRY_VERIFY(toolBar(m_left)->scope());
    toolBar(m_left)->scope()->action(QStringLiteral("go_up"))->trigger();
    QTRY_COMPARE(normalized(m_right->url()), normalized(m_dir->url()));
    QTRY_COMPARE(current(m_left), QStringLiteral("sub"));
    QTRY_COMPARE(current(m_right), QStringLiteral("sub"));
    QCOMPARE(selected(m_right), selected(m_left));
}

void WhaleSplitLockTest::testMirrorLeavesOutFilteredItems()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_right->view()->setNameFilter(QStringLiteral("sub"));
    QTRY_COMPARE(m_right->view()->itemsCount(), 1);
    m_left->setActive(true);
    lock()->setLocked(true);
    select(m_left, {QStringLiteral("a.txt"), QStringLiteral("sub")}, QStringLiteral("a.txt"));
    QCOMPARE(selected(m_right), QStringList{QStringLiteral("sub")});
    // Shown again: not selected later by surprise.
    m_right->view()->setNameFilter(QString());
    QTRY_COMPARE(m_right->view()->itemsCount(), 2);
    QTest::qWait(100);
    QCOMPARE(selected(m_right), QStringList{QStringLiteral("sub")});
}

QTEST_MAIN(WhaleSplitLockTest)

QWidget *WhaleSplitLockTest::showPluginView(DolphinViewContainer *container, const QString &pluginId)
{
    WhaleViewPlugin *plugin = WhaleViewServices::of(m_mainWindow.data())->registry()->plugin(pluginId);
    if (!plugin || !container->viewHost()->showPluginView(pluginId, plugin)) {
        return nullptr;
    }
    QWidget *view = container->viewHost()->currentView()->widget();
    if (pluginId == treemapId
        && !QTest::qWaitFor(
            [view]() {
                return treemapState(view).value(QStringLiteral("status")).toString() == QLatin1String("ready");
            },
            5000)) {
        return nullptr;
    }
    return view;
}

bool WhaleSplitLockTest::selectInTreemap(QWidget *view, const QUrl &target)
{
    QRect rect;
    QMetaObject::invokeMethod(view, "cellRect", Qt::DirectConnection, Q_RETURN_ARG(QRect, rect), Q_ARG(QUrl, target));
    QWidget *canvas = view->findChild<QWidget *>(QStringLiteral("whaleTreemapCanvas"));
    if (rect.isEmpty() || !canvas) {
        return false;
    }
    QTest::mouseClick(canvas, Qt::LeftButton, {}, rect.center());
    for (int step = 0; step < 10; ++step) {
        if (treemapSelection(view) == target) {
            return true;
        }
        QTest::keyClick(canvas, Qt::Key_Up);
    }
    return false;
}

void WhaleSplitLockTest::testLockWithTreemap()
{
    // LK5: the treemap can be linked, the lock stays when it is shown.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    QVERIFY(showPluginView(m_left, treemapId));
    QTRY_VERIFY(lock()->button()->isEnabled());
    lock()->setLocked(true);
    QVERIFY(lock()->isLocked());
    QVERIFY(showPluginView(m_right, treemapId));
    m_right->viewHost()->showDolphinView();
    QTest::qWait(100);
    QVERIFY(lock()->isLocked());
}

void WhaleSplitLockTest::testMirrorFromTreemap()
{
    // The treemap's selection, also deep, in the other view.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_right->view()->setViewMode(DolphinView::DetailsView);
    QWidget *view = showPluginView(m_left, treemapId);
    QVERIFY(view);
    m_left->setActive(true);
    lock()->setLocked(true);

    QVERIFY(selectInTreemap(view, url(QStringLiteral("a.txt"))));
    QTRY_COMPARE(selected(m_right), QStringList{QStringLiteral("a.txt")});
    QCOMPARE(current(m_right), QStringLiteral("a.txt"));

    QVERIFY(selectInTreemap(view, url(QStringLiteral("sub/deeper/c.txt"))));
    QTRY_COMPARE(selected(m_right), QStringList{QStringLiteral("sub/deeper/c.txt")});
    QCOMPARE(current(m_right), QStringLiteral("sub/deeper/c.txt"));
    // The deep entry stays the treemap's own.
    QCOMPARE(treemapSelection(view), url(QStringLiteral("sub/deeper/c.txt")));
    QVERIFY(m_left->isActive());
}

void WhaleSplitLockTest::testMirrorIntoTreemap()
{
    // The other view's selection in the treemap: one entry, also deep.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->view()->setViewMode(DolphinView::DetailsView);
    QWidget *view = showPluginView(m_right, treemapId);
    QVERIFY(view);
    m_left->setActive(true);
    lock()->setLocked(true);

    select(m_left, {QStringLiteral("a.txt")}, QStringLiteral("a.txt"));
    QTRY_COMPARE(treemapSelection(view), url(QStringLiteral("a.txt")));

    // Several: the current one.
    select(m_left, {QStringLiteral("a.txt"), QStringLiteral("sub")}, QStringLiteral("sub"));
    QTRY_COMPARE(treemapSelection(view), url(QStringLiteral("sub")));

    QVERIFY(model(m_left)->setExpanded(model(m_left)->index(url(QStringLiteral("sub"))), true));
    QTRY_VERIFY(model(m_left)->index(url(QStringLiteral("sub/b.txt"))) >= 0);
    select(m_left, {QStringLiteral("sub/b.txt")}, QStringLiteral("sub/b.txt"));
    QTRY_COMPARE(treemapSelection(view), url(QStringLiteral("sub/b.txt")));

    select(m_left, {}, QStringLiteral("a.txt"));
    QTRY_VERIFY(treemapSelection(view).isEmpty());

    // Nothing comes back: the left view keeps its state.
    QTest::qWait(100);
    QVERIFY(m_left->isActive());
    QVERIFY(selected(m_left).isEmpty());
    QCOMPARE(current(m_left), QStringLiteral("a.txt"));
}

#include "whalesplitlocktest.moc"
