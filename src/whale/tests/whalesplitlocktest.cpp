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
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalefileviewtoolbar.h"
#include "whalesplitviewbutton.h"
#include "whaletoolbarsettings.h"
#include "whaleviewcommandscope.h"
#include "whaleviewhost.h"
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

private:
    DolphinViewContainer *showMainWindow();
    void showSplit();
    DolphinTabPage *tabPage() const;
    WhaleSplitLock *lock() const;
    static WhaleFileViewToolBar *toolBar(DolphinViewContainer *container);
    QUrl url(const QString &relative) const;
    static QUrl normalized(const QUrl &url);

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
    KLocalizedString::setApplicationDomain({"whale"});
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
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
    WhaleViewPlugin *plugin = WhaleViewServices::of(m_mainWindow.data())->registry()->plugin(treemapId);
    QVERIFY(plugin);
    QVERIFY(m_right->viewHost()->showPluginView(treemapId, plugin));
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
    // Until plugin views can show the selection of the other view (LK5), showing one releases
    // the lock.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    lock()->setLocked(true);
    WhaleViewPlugin *plugin = WhaleViewServices::of(m_mainWindow.data())->registry()->plugin(treemapId);
    QVERIFY(plugin);
    QVERIFY(m_left->viewHost()->showPluginView(treemapId, plugin));
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
    // A8: information that hides itself; the same message again restarts the time, another
    // message is not hidden by it.
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    container->showTransientMessage(QStringLiteral("first"));
    QTRY_COMPARE(messageText(container), QStringLiteral("first"));
    QCOMPARE(shownMessage(container)->messageType(), KMessageWidget::Information);
    QTRY_VERIFY_WITH_TIMEOUT(messageText(container).isEmpty(), 8000);

    container->showTransientMessage(QStringLiteral("again"));
    QTRY_COMPARE(messageText(container), QStringLiteral("again"));
    QTest::qWait(3000);
    container->showTransientMessage(QStringLiteral("again"));
    QTest::qWait(2000);
    QCOMPARE(messageText(container), QStringLiteral("again"));
    QTRY_VERIFY_WITH_TIMEOUT(messageText(container).isEmpty(), 8000);

    container->showTransientMessage(QStringLiteral("transient"));
    container->showMessage(QStringLiteral("error"), KMessageWidget::Error);
    QTest::qWait(5000);
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

QTEST_MAIN(WhaleSplitLockTest)

#include "whalesplitlocktest.moc"
