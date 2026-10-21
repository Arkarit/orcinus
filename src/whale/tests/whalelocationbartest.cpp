/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalelocationbar.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinurlnavigator.h"
#include "dolphinviewcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalefileviewtoolbar.h"
#include "whalefileviewtoolbarclient.h"
#include "whaletoolbarmanager.h"
#include "whaletoolbarsettings.h"
#include "whaleview.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"
#include "whaleviewservices.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KUrlComboBox>

#include <QAbstractButton>
#include <QApplication>
#include <QDockWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>
#include <QVariantAnimation>

#include <algorithm>

namespace
{
QString localLayoutFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kxmlgui5/") + QStringLiteral(WHALE_COMPONENT)
        + QLatin1Char('/') + WhaleFileViewToolbarClient::rcFile();
}

// The visible breadcrumb button of @p navigator with the text @p text.
QAbstractButton *breadcrumb(KUrlNavigator *navigator, const QString &text)
{
    const auto buttons = navigator->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        if (button->isVisible() && button->text().remove(QLatin1Char('&')) == text) {
            return button;
        }
    }
    return nullptr;
}

QStringList overflowed(const QToolBar *toolBar)
{
    QStringList result;
    const auto actions = toolBar->actions();
    for (QAction *action : actions) {
        const QWidget *widget = toolBar->widgetForAction(action);
        if (widget && !widget->isVisible()) {
            result.append(action->objectName());
        }
    }
    return result;
}
}

/**
 * The location bar of each file view (location bar plan, LB1/LB2): one per view, connected
 * to it for good, placed by the File View Toolbar; no global location bars.
 */
class WhaleLocationBarTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSingleView();
    void testSplit();
    void testHistory();
    void testTabs();
    void testCloseSplit_data();
    void testCloseSplit();
    void testWindowActions();
    void testHiddenToolBar();
    void testLayout();
    void testSpacer();
    void testStableWidth();
    void testContextButtons();
    void testPluginView();
    void testThirdContainer();
    void testFocusOrder();
    void testWrap();
    void testWrapSplit();
    void testWrapFocusOrder();

private:
    DolphinViewContainer *showMainWindow(int width = 1200);
    void showSplit();
    DolphinTabWidget *tabWidget() const;
    DolphinTabPage *tabPage() const;
    static WhaleFileViewToolBar *toolBar(DolphinViewContainer *container);
    static WhaleLocationBar *locationBar(DolphinViewContainer *container);
    static void writeUserLayout(const QByteArray &toolBarContent);
    QAction *global(const QString &name) const;
    QUrl url(const QString &relative) const;
    void checkTabCycle(bool forward);
    void setLeftShare(double share);
    static void checkWrapped(WhaleFileViewToolBar *bar);

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    DolphinViewContainer *m_left = nullptr;
    DolphinViewContainer *m_right = nullptr;
};

void WhaleLocationBarTest::initTestCase()
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
}

void WhaleLocationBarTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(3000, 'a'));
    m_dir->createFile(QStringLiteral("sub/deeper/folder/b.txt"), QByteArray(1000, 'b'));
    m_dir->createFile(QStringLiteral("sub2/c.txt"), QByteArray(500, 'c'));
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleLocationBarTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
    m_left = m_right = nullptr;
    QFile::remove(localLayoutFile());
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    GeneralSettings::self()->setCloseSplitViewChoice(GeneralSettings::EnumCloseSplitViewChoice::ActiveView);
}

DolphinViewContainer *WhaleLocationBarTest::showMainWindow(int width)
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(width, 700);
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

void WhaleLocationBarTest::showSplit()
{
    QVERIFY(showMainWindow());
    global(QStringLiteral("split_view"))->trigger();
    m_left = tabPage()->primaryViewContainer();
    m_right = tabPage()->secondaryViewContainer();
    QVERIFY(m_left && m_right);
    QTRY_COMPARE(m_right->view()->itemsCount(), 3);
    QTRY_VERIFY(locationBar(m_left)->isVisible() && locationBar(m_right)->isVisible());
}

DolphinTabWidget *WhaleLocationBarTest::tabWidget() const
{
    return m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"));
}

DolphinTabPage *WhaleLocationBarTest::tabPage() const
{
    return tabWidget()->currentTabPage();
}

WhaleFileViewToolBar *WhaleLocationBarTest::toolBar(DolphinViewContainer *container)
{
    return container->findChild<WhaleFileViewToolBar *>(QString(), Qt::FindDirectChildrenOnly);
}

WhaleLocationBar *WhaleLocationBarTest::locationBar(DolphinViewContainer *container)
{
    return toolBar(container)->locationBar();
}

void WhaleLocationBarTest::writeUserLayout(const QByteArray &toolBarContent)
{
    const QString file = localLayoutFile();
    QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
    QFile local(file);
    QVERIFY(local.open(QIODevice::WriteOnly));
    local.write(
        "<!DOCTYPE gui SYSTEM \"kpartgui.dtd\">\n<gui name=\"whale_fileview\" version=\"1\">"
        "<ToolBar noMerge=\"1\" name=\"whaleFileViewToolBar\"><text>File View Toolbar</text>"
        + toolBarContent + "</ToolBar></gui>");
    local.close();
    WhaleToolbarManager::reloadAllFileViewLayouts();
}

QAction *WhaleLocationBarTest::global(const QString &name) const
{
    return m_mainWindow->actionCollection()->action(name);
}

QUrl WhaleLocationBarTest::url(const QString &relative) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + relative);
}

void WhaleLocationBarTest::testSingleView()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    // No global location bars any more (L5).
    QVERIFY(!global(QStringLiteral("url_navigators")));
    QCOMPARE(m_mainWindow->findChildren<WhaleLocationBar *>().count(), 1);
    WhaleLocationBar *bar = locationBar(container);
    QTRY_VERIFY(bar->isVisible());
    QCOMPARE(bar->parentWidget(), toolBar(container));
    QCOMPARE(toolBar(container)->widgetForAction(bar->action()), bar);
    // The visible navigator of the view is the location bar's.
    DolphinUrlNavigator *navigator = bar->urlNavigator();
    QCOMPARE(container->urlNavigator(), navigator);
    QVERIFY(container->urlNavigatorInternalWithHistory() != navigator);
    QCOMPARE(m_mainWindow->findChildren<DolphinUrlNavigator *>().count(), 1);

    // The URL follows the view ...
    QCOMPARE(navigator->locationUrl().adjusted(QUrl::StripTrailingSlash), m_dir->url());
    container->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(navigator->locationUrl(), url(QStringLiteral("sub")));
    // ... the breadcrumb navigates the view ...
    container->setUrl(url(QStringLiteral("sub/deeper/folder")));
    QTRY_VERIFY(breadcrumb(navigator, QStringLiteral("deeper")));
    QTest::mouseClick(breadcrumb(navigator, QStringLiteral("deeper")), Qt::LeftButton);
    QTRY_COMPARE(container->url().adjusted(QUrl::StripTrailingSlash), url(QStringLiteral("sub/deeper")));
    // ... and so does a location typed in the editable navigator.
    navigator->setUrlEditable(true);
    QVERIFY(navigator->editor()->isVisible());
    navigator->editor()->lineEdit()->setText(url(QStringLiteral("sub2")).toLocalFile());
    QTest::keyClick(navigator->editor()->lineEdit(), Qt::Key_Return);
    QTRY_COMPARE(container->url().adjusted(QUrl::StripTrailingSlash), url(QStringLiteral("sub2")));
    navigator->setUrlEditable(false);
    QVERIFY(!navigator->editor()->isVisible());
}

void WhaleLocationBarTest::testSplit()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    WhaleLocationBar *left = locationBar(m_left);
    WhaleLocationBar *right = locationBar(m_right);
    QVERIFY(left != right);
    QCOMPARE(m_mainWindow->findChildren<WhaleLocationBar *>().count(), 2);
    QCOMPARE(m_left->urlNavigator(), left->urlNavigator());
    QCOMPARE(m_right->urlNavigator(), right->urlNavigator());

    m_right->setUrl(url(QStringLiteral("sub/deeper")));
    QTRY_COMPARE(right->urlNavigator()->locationUrl(), url(QStringLiteral("sub/deeper")));
    QCOMPARE(left->urlNavigator()->locationUrl().adjusted(QUrl::StripTrailingSlash), m_dir->url());

    // Working with the inactive view's location bar makes that view the active one; the
    // other view stays where it is.
    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive() && !m_right->isActive());
    QTRY_VERIFY(breadcrumb(right->urlNavigator(), QStringLiteral("sub")));
    QTest::mouseClick(breadcrumb(right->urlNavigator(), QStringLiteral("sub")), Qt::LeftButton);
    QTRY_COMPARE(m_right->url().adjusted(QUrl::StripTrailingSlash), url(QStringLiteral("sub")));
    QTRY_VERIFY(m_right->isActive() && !m_left->isActive());
    QCOMPARE(m_mainWindow->activeViewContainer(), m_right);
    QCOMPARE(m_left->url().adjusted(QUrl::StripTrailingSlash), m_dir->url());
    QCOMPARE(left->urlNavigator()->locationUrl().adjusted(QUrl::StripTrailingSlash), m_dir->url());

    // Editing in the left one activates the left view.
    left->urlNavigator()->setUrlEditable(true);
    QTRY_VERIFY(m_left->isActive() && !m_right->isActive());
    QVERIFY(!right->urlNavigator()->isUrlEditable());
}

void WhaleLocationBarTest::testHistory()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    WhaleLocationBar *bar = locationBar(container);
    DolphinUrlNavigator *navigator = bar->urlNavigator();
    navigator->setLocationUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("sub")));
    navigator->setLocationUrl(url(QStringLiteral("sub2")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("sub2")));
    KUrlNavigator *history = container->urlNavigatorInternalWithHistory();
    QCOMPARE(history->historySize(), 3);

    // Back and Forward of the window: the history of the view, the location bar follows.
    global(QStringLiteral("go_back"))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("sub")));
    QTRY_COMPARE(navigator->locationUrl(), url(QStringLiteral("sub")));
    global(QStringLiteral("go_forward"))->trigger();
    QTRY_COMPARE(navigator->locationUrl(), url(QStringLiteral("sub2")));

    // Taking the location bar out of the toolbar, putting it back and hiding the toolbar
    // changes nothing about the history.
    writeUserLayout("<Action name=\"go_back\"/><Action name=\"go_up\"/>");
    QVERIFY(!bar->isVisible());
    QCOMPARE(container->urlNavigator(), navigator);
    writeUserLayout("<Action name=\"go_back\"/><Action name=\"whale_location_bar\"/>");
    QTRY_VERIFY(bar->isVisible());
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    QTRY_VERIFY(bar->isVisible());
    QCOMPARE(locationBar(container), bar);
    QCOMPARE(history->historySize(), 3);
    QCOMPARE(history->historyIndex(), 0);
    global(QStringLiteral("go_back"))->trigger();
    QTRY_COMPARE(container->url(), url(QStringLiteral("sub")));
    QTRY_COMPARE(navigator->locationUrl(), url(QStringLiteral("sub")));
    global(QStringLiteral("go_back"))->trigger();
    QTRY_COMPARE(container->url().adjusted(QUrl::StripTrailingSlash), m_dir->url());
}

void WhaleLocationBarTest::testTabs()
{
    DolphinViewContainer *first = showMainWindow();
    QVERIFY(first);
    WhaleLocationBar *firstBar = locationBar(first);
    firstBar->urlNavigator()->setUrlEditable(true);
    firstBar->urlNavigator()->editor()->lineEdit()->setText(QStringLiteral("typed"));

    // A new tab takes the mode of the active location bar over (Dolphin's behaviour).
    QTRY_VERIFY(global(QStringLiteral("new_tab"))->isEnabled());
    global(QStringLiteral("new_tab"))->trigger();
    QTRY_COMPARE(tabWidget()->count(), 2);
    DolphinViewContainer *second = m_mainWindow->activeViewContainer();
    QVERIFY(second != first);
    WhaleLocationBar *secondBar = locationBar(second);
    QVERIFY(secondBar != firstBar);
    QCOMPARE(second->urlNavigator(), secondBar->urlNavigator());
    QVERIFY(secondBar->urlNavigator()->isUrlEditable());
    QTRY_VERIFY(secondBar->isVisible());
    QVERIFY(!firstBar->isVisible());
    secondBar->urlNavigator()->setUrlEditable(false);

    // Switching tabs connects nothing anew: every view keeps its location bar and its state.
    tabWidget()->setCurrentIndex(0);
    QTRY_VERIFY(firstBar->isVisible());
    QCOMPARE(first->urlNavigator(), firstBar->urlNavigator());
    QCOMPARE(second->urlNavigator(), secondBar->urlNavigator());
    QVERIFY(firstBar->urlNavigator()->isUrlEditable());
    QCOMPARE(firstBar->urlNavigator()->editor()->lineEdit()->text(), QStringLiteral("typed"));
    QVERIFY(!secondBar->urlNavigator()->isUrlEditable());
    QCOMPARE(m_mainWindow->findChildren<WhaleLocationBar *>().count(), 2);
}

void WhaleLocationBarTest::testCloseSplit_data()
{
    QTest::addColumn<GeneralSettings::EnumCloseSplitViewChoice::type>("choice");
    QTest::addColumn<bool>("leftActive");
    QTest::addColumn<bool>("leftSurvives");
    QTest::addRow("active view, left active") << GeneralSettings::EnumCloseSplitViewChoice::ActiveView << true << false;
    QTest::addRow("active view, right active") << GeneralSettings::EnumCloseSplitViewChoice::ActiveView << false << true;
    QTest::addRow("inactive view, left active") << GeneralSettings::EnumCloseSplitViewChoice::InactiveView << true << true;
    QTest::addRow("inactive view, right active") << GeneralSettings::EnumCloseSplitViewChoice::InactiveView << false << false;
    QTest::addRow("right view") << GeneralSettings::EnumCloseSplitViewChoice::RightView << true << true;
}

void WhaleLocationBarTest::testCloseSplit()
{
    QFETCH(GeneralSettings::EnumCloseSplitViewChoice::type, choice);
    QFETCH(bool, leftActive);
    QFETCH(bool, leftSurvives);
    GeneralSettings::self()->setCloseSplitViewChoice(choice);
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    m_left->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(m_left->url(), url(QStringLiteral("sub")));
    m_right->setUrl(url(QStringLiteral("sub2")));
    QTRY_COMPARE(m_right->url(), url(QStringLiteral("sub2")));
    (leftActive ? m_left : m_right)->setActive(true);
    QTRY_VERIFY((leftActive ? m_left : m_right)->isActive());

    QPointer<DolphinViewContainer> survivor = leftSurvives ? m_left : m_right;
    QPointer<DolphinViewContainer> closing = leftSurvives ? m_right : m_left;
    QPointer<WhaleLocationBar> survivorBar = locationBar(survivor);
    QPointer<WhaleLocationBar> closingBar = locationBar(closing);
    const QUrl survivorUrl = survivor->url();

    global(QStringLiteral("split_view"))->trigger();
    QTRY_VERIFY(!closing);
    QTRY_VERIFY(!closingBar);
    QVERIFY(survivor && survivorBar);
    QCOMPARE(tabPage()->activeViewContainer(), survivor.data());
    // The view that stays keeps its location bar and its history.
    QCOMPARE(survivor->urlNavigator(), survivorBar->urlNavigator());
    QCOMPARE(survivorBar->urlNavigator()->locationUrl(), survivorUrl);
    QTRY_VERIFY(survivorBar->isVisible());
    QCOMPARE(m_mainWindow->findChildren<WhaleLocationBar *>().count(), 1);
    global(QStringLiteral("go_back"))->trigger();
    QTRY_COMPARE(survivor->url().adjusted(QUrl::StripTrailingSlash), m_dir->url());
    QTRY_COMPARE(survivorBar->urlNavigator()->locationUrl().adjusted(QUrl::StripTrailingSlash), m_dir->url());

    // Splitting again: a new location bar for the new view.
    global(QStringLiteral("split_view"))->trigger();
    QTRY_VERIFY(tabPage()->secondaryViewContainer());
    DolphinViewContainer *added = tabPage()->secondaryViewContainer();
    QVERIFY(locationBar(added) != survivorBar);
    QCOMPARE(added->urlNavigator(), locationBar(added)->urlNavigator());
}

void WhaleLocationBarTest::testWindowActions()
{
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    // Ctrl+L and F6 act on the active view's location bar; F6's check follows the active view.
    QAction *replaceLocation = global(QStringLiteral("replace_location"));
    QAction *editableLocation = global(QStringLiteral("editable_location"));
    m_right->setActive(true);
    QTRY_VERIFY(m_right->isActive());
    replaceLocation->trigger();
    QVERIFY(locationBar(m_right)->urlNavigator()->isUrlEditable());
    QTRY_VERIFY(locationBar(m_right)->urlNavigator()->editor()->lineEdit()->hasFocus());
    QVERIFY(!locationBar(m_left)->urlNavigator()->isUrlEditable());
    QVERIFY(editableLocation->isChecked());

    m_left->setActive(true);
    QTRY_VERIFY(m_left->isActive());
    QVERIFY(!editableLocation->isChecked());
    editableLocation->trigger();
    QVERIFY(locationBar(m_left)->urlNavigator()->isUrlEditable());
    QVERIFY(editableLocation->isChecked());
    editableLocation->trigger();
    QVERIFY(!locationBar(m_left)->urlNavigator()->isUrlEditable());
    QVERIFY(locationBar(m_right)->urlNavigator()->isUrlEditable());
    QVERIFY(!editableLocation->isChecked());
    // The location bar is not offered for a button of its own in the File View Toolbar.
    QVERIFY(!toolBar(m_left)->actions().contains(replaceLocation));
}

void WhaleLocationBarTest::testHiddenToolBar()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    WhaleLocationBar *bar = locationBar(container);
    QTRY_VERIFY(bar->isVisible());
    // Hidden File View Toolbars hide the location bars (L7) ...
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    QVERIFY(!bar->isVisible());
    // ... which stay connected: navigating and Ctrl+L work, without anything to see.
    QCOMPARE(container->urlNavigator(), bar->urlNavigator());
    container->setUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(bar->urlNavigator()->locationUrl(), url(QStringLiteral("sub")));
    global(QStringLiteral("replace_location"))->trigger();
    QVERIFY(bar->urlNavigator()->isUrlEditable());
    QVERIFY(!bar->isVisible());
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    QTRY_VERIFY(bar->isVisible());

    // A view whose toolbar was never shown has a connected location bar, too.
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    global(QStringLiteral("new_tab"))->trigger();
    QTRY_COMPARE(tabWidget()->count(), 2);
    DolphinViewContainer *hidden = m_mainWindow->activeViewContainer();
    QVERIFY(!toolBar(hidden)->scope());
    QCOMPARE(hidden->urlNavigator(), locationBar(hidden)->urlNavigator());
    // It took the editable mode of the location bar before over (Ctrl+L above); F6 acts on it.
    QVERIFY(locationBar(hidden)->urlNavigator()->isUrlEditable());
    QVERIFY(global(QStringLiteral("editable_location"))->isChecked());
    global(QStringLiteral("editable_location"))->trigger();
    QVERIFY(!locationBar(hidden)->urlNavigator()->isUrlEditable());
    QVERIFY(bar->urlNavigator()->isUrlEditable());
}

void WhaleLocationBarTest::testLayout()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    WhaleFileViewToolBar *bar = toolBar(container);
    QPointer<WhaleLocationBar> location = bar->locationBar();
    QTRY_VERIFY(location->isVisible());

    // Twice in a hand-edited file: at its first place only, and never a second widget.
    writeUserLayout("<Action name=\"whale_location_bar\"/><Action name=\"go_back\"/><Action name=\"whale_location_bar\"/>");
    QStringList layout;
    for (QAction *action : bar->actions()) {
        layout.append(action->objectName());
    }
    QCOMPARE(layout, QStringList({QStringLiteral("whale_location_bar"), QStringLiteral("go_back")}));
    QTRY_VERIFY(location->isVisible());
    QCOMPARE(bar->locationBar(), location.data());
    QCOMPARE(m_mainWindow->findChildren<WhaleLocationBar *>().count(), 1);
    QCOMPARE(bar->findChildren<QWidgetAction *>(QString(), Qt::FindDirectChildrenOnly).count(), 1);

    // Not in the layout: hidden, still the toolbar's and connected, the editor state kept.
    location->urlNavigator()->setUrlEditable(true);
    writeUserLayout("<Action name=\"go_back\"/>");
    QVERIFY(location && !location->isVisible());
    QCOMPARE(location->parentWidget(), bar);
    QCOMPARE(container->urlNavigator(), location->urlNavigator());
    writeUserLayout("<Action name=\"go_back\"/><Action name=\"whale_location_bar\"/>");
    QTRY_VERIFY(location->isVisible());
    QVERIFY(location->urlNavigator()->isUrlEditable());
    QCOMPARE(bar->findChildren<QWidgetAction *>(QString(), Qt::FindDirectChildrenOnly).count(), 1);

    // An unknown "url_navigators" (a file of an older Whale) shows nothing.
    writeUserLayout("<Action name=\"url_navigators\"/><Action name=\"go_back\"/>");
    QCOMPARE(bar->actions().count(), 1);
}

void WhaleLocationBarTest::testSpacer()
{
    writeUserLayout("<Action name=\"go_back\"/><Action name=\"whale_location_bar\"/><Spacer/><Action name=\"view_settings\"/>");
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    WhaleFileViewToolBar *bar = toolBar(container);
    WhaleLocationBar *location = bar->locationBar();
    QTRY_VERIFY(location->isVisible());
    QWidget *spacer = nullptr;
    for (QAction *action : bar->actions()) {
        if (action->objectName().isEmpty() && !action->isSeparator()) {
            spacer = bar->widgetForAction(action);
        }
    }
    QVERIFY(spacer);
    // The location bar takes the room; the spacer does not take half of it.
    QTRY_VERIFY(location->width() > bar->width() / 2);
    QVERIFY(spacer->width() < 20);
    // Navigating and editing do not change that.
    location->urlNavigator()->setUrlEditable(true);
    QTRY_VERIFY(location->width() > bar->width() / 2);
    QVERIFY(spacer->width() < 20);

    // Without a location bar a spacer expands as ever.
    writeUserLayout("<Action name=\"go_back\"/><Spacer/><Action name=\"view_settings\"/>");
    for (QAction *action : bar->actions()) {
        if (action->objectName().isEmpty() && !action->isSeparator()) {
            spacer = bar->widgetForAction(action);
        }
    }
    QTRY_VERIFY(spacer->width() > bar->width() / 2);
}

void WhaleLocationBarTest::testStableWidth()
{
    // Many buttons after the location bar in a narrow view: the ones that do not fit are in
    // the overflow menu, and which ones those are does not change with the location or the
    // mode of the location bar. The location bar never is: it moves into the second row (L16).
    writeUserLayout(
        "<Action name=\"go_back\"/><Action name=\"go_forward\"/><Action name=\"whale_location_bar\"/><Action name=\"view_settings\"/>"
        "<Action name=\"go_home\"/><Action name=\"show_filter_bar\"/><Action name=\"show_hidden_files\"/><Action name=\"show_preview\"/>"
        "<Action name=\"view_zoom_in\"/><Action name=\"view_zoom_out\"/><Action name=\"sort\"/><Action name=\"icons\"/><Action name=\"details\"/>");
    DolphinViewContainer *container = showMainWindow(640);
    QVERIFY(container);
    if (auto places = m_mainWindow->findChild<QDockWidget *>(QStringLiteral("placesDock"))) {
        places->hide();
    }
    WhaleFileViewToolBar *bar = toolBar(container);
    WhaleLocationBar *location = bar->locationBar();
    DolphinUrlNavigator *navigator = location->urlNavigator();
    QTRY_VERIFY(location->isVisible());
    // No minimum width of its own: what the navigator needs (KUrlNavigator sets 100 px).
    QCOMPARE(location->minimumWidth(), 0);
    QCOMPARE(location->minimumSizeHint().width(), navigator->minimumWidth());

    auto settle = []() {
        QCoreApplication::processEvents();
        QTest::qWait(200); // also for the decision about the second row
    };
    auto show = [&](const QUrl &location, bool editable) {
        navigator->setUrlEditable(editable);
        container->setUrl(location);
        QTRY_COMPARE(navigator->locationUrl().adjusted(QUrl::StripTrailingSlash), location.adjusted(QUrl::StripTrailingSlash));
        settle();
    };
    // From a toolbar where all fits down to one where most buttons are in the overflow menu.
    int overflowing = 0;
    for (int windowWidth = 640; windowWidth >= 260; windowWidth -= 20) {
        m_mainWindow->resize(windowWidth, 700);
        show(m_dir->url(), false);
        const QStringList shortPath = overflowed(bar);
        const int width = location->width();
        QVERIFY(!shortPath.contains(WhaleLocationBar::actionName()));
        show(url(QStringLiteral("sub/deeper/folder")), false);
        QCOMPARE(overflowed(bar), shortPath);
        QCOMPARE(location->width(), width);
        show(url(QStringLiteral("sub/deeper/folder")), true);
        QCOMPARE(overflowed(bar), shortPath);
        QCOMPARE(location->width(), width);
        show(m_dir->url(), true);
        QCOMPARE(overflowed(bar), shortPath);
        if (!shortPath.isEmpty()) {
            ++overflowing;
        }
    }
    navigator->setUrlEditable(false);
    QVERIFY2(overflowing > 0, "the test needs a toolbar that overflows");
}

void WhaleLocationBarTest::testContextButtons()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    WhaleLocationBar *bar = locationBar(container);
    DolphinUrlNavigator *navigator = bar->urlNavigator();
    QPushButton *emptyTrash = nullptr;
    for (QPushButton *button : bar->findChildren<QPushButton *>()) {
        if (button->text().remove(QLatin1Char('&')) == QLatin1String("Empty Trash")) {
            emptyTrash = button;
        }
    }
    QVERIFY(emptyTrash);
    QVERIFY(!emptyTrash->isVisible());

    container->setUrl(QUrl(QStringLiteral("trash:/")));
    QTRY_COMPARE(navigator->locationUrl().scheme(), QStringLiteral("trash"));
    QTRY_VERIFY(emptyTrash->isVisible());

    // remote: asks for a server URL in the editable navigator (Dolphin bug 414670).
    container->setUrl(QUrl(QStringLiteral("remote:/")));
    QTRY_COMPARE(navigator->locationUrl().scheme(), QStringLiteral("remote"));
    QTRY_VERIFY(navigator->isUrlEditable());
    QTRY_VERIFY(!navigator->editor()->lineEdit()->placeholderText().isEmpty());
    QVERIFY(!emptyTrash->isVisible());

    container->setUrl(m_dir->url());
    QTRY_COMPARE(navigator->locationUrl().scheme(), QStringLiteral("file"));
    QTRY_VERIFY(navigator->editor()->lineEdit()->placeholderText().isEmpty());
    QVERIFY(!emptyTrash->isVisible());
}

void WhaleLocationBarTest::testPluginView()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QPointer<WhaleLocationBar> bar = locationBar(container);
    DolphinUrlNavigator *navigator = bar->urlNavigator();
    const QString treemapId = QStringLiteral("whale_treemap");
    WhaleViewPlugin *plugin = WhaleViewServices::of(m_mainWindow.data())->registry()->plugin(treemapId);
    QVERIFY(plugin);
    QVERIFY(container->viewHost()->showPluginView(treemapId, plugin));
    QTRY_COMPARE(container->viewHost()->currentViewId(), treemapId);
    // The same location bar, still the view's.
    QCOMPARE(locationBar(container), bar.data());
    QCOMPARE(container->urlNavigator(), navigator);
    QVERIFY(bar->isVisible());
    navigator->setLocationUrl(url(QStringLiteral("sub")));
    QTRY_COMPARE(container->url(), url(QStringLiteral("sub")));
    container->viewHost()->showDolphinView();
    QCOMPARE(locationBar(container), bar.data());
    QCOMPARE(container->urlNavigator(), navigator);
}

void WhaleLocationBarTest::testThirdContainer()
{
    // A file view outside of the tab pages, as a dock of the window would hold it.
    QVERIFY(showMainWindow());
    auto dock = new QDockWidget(QStringLiteral("Third view"), m_mainWindow.data());
    auto third = new DolphinViewContainer(url(QStringLiteral("sub")), dock);
    dock->setWidget(third);
    m_mainWindow->addDockWidget(Qt::BottomDockWidgetArea, dock);
    third->show();
    dock->show();
    QPointer<WhaleLocationBar> bar = locationBar(third);
    QVERIFY(bar);
    QCOMPARE(third->urlNavigator(), bar->urlNavigator());
    QTRY_VERIFY(bar->isVisible());
    QTRY_COMPARE(bar->urlNavigator()->locationUrl(), url(QStringLiteral("sub")));
    DolphinViewContainer *tabView = m_mainWindow->activeViewContainer();
    QTRY_VERIFY(breadcrumb(bar->urlNavigator(), QFileInfo(m_dir->path()).fileName()));
    QTest::mouseClick(breadcrumb(bar->urlNavigator(), QFileInfo(m_dir->path()).fileName()), Qt::LeftButton);
    QTRY_COMPARE(third->url().adjusted(QUrl::StripTrailingSlash), m_dir->url());
    QCOMPARE(tabView->url(), m_dir->url());
    delete dock;
    QVERIFY(!bar);
}

void WhaleLocationBarTest::testFocusOrder()
{
    // Tab goes through each view from top to bottom, its File View Toolbar first, and the view
    // with the focus is the active one.
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    // Shift+Tab from a file list: into its own location bar, not into the one of the other view
    // (the toolbars were created after the views).
    m_right->setActive(true);
    QTRY_VERIFY(m_right->view()->isAncestorOf(QApplication::focusWidget()));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab, Qt::ShiftModifier);
    QVERIFY(locationBar(m_right)->isAncestorOf(QApplication::focusWidget()));
    QVERIFY(m_right->isActive());
    checkTabCycle(true);
    checkTabCycle(false);
    if (QTest::currentTestFailed()) {
        return;
    }

    // The buttons of a deeper location are created now, at the end of Qt's order.
    m_right->setUrl(url(QStringLiteral("sub/deeper/folder")));
    QTRY_VERIFY(breadcrumb(locationBar(m_right)->urlNavigator(), QStringLiteral("deeper")));
    QTest::qWait(0);
    checkTabCycle(true);
    checkTabCycle(false);
    if (QTest::currentTestFailed()) {
        return;
    }

    // And after the toolbars were built again (Configure Toolbars).
    writeUserLayout("<Action name=\"go_back\"/><Action name=\"whale_location_bar\"/><Action name=\"go_up\"/>");
    QTRY_VERIFY(locationBar(m_left)->isVisible() && locationBar(m_right)->isVisible());
    QTest::qWait(0);
    checkTabCycle(true);
    checkTabCycle(false);
}

void WhaleLocationBarTest::checkTabCycle(bool forward)
{
    // Once around, from the active view's file list.
    m_left->setActive(true);
    QTRY_VERIFY(m_left->view()->isAncestorOf(QApplication::focusWidget()));
    QWidget *start = QApplication::focusWidget();
    QList<QWidget *> cycle;
    QList<DolphinViewContainer *> containers;
    do {
        QWidget *focus = QApplication::focusWidget();
        QVERIFY(focus);
        DolphinViewContainer *container = nullptr;
        for (DolphinViewContainer *candidate : {m_left, m_right}) {
            if (candidate->isAncestorOf(focus)) {
                container = candidate;
            }
        }
        // The view with the focus in its toolbar or file list is the active one.
        if (container && (toolBar(container)->isAncestorOf(focus) || container->view()->isAncestorOf(focus))) {
            QVERIFY2(container->isActive(), qPrintable(QString::fromLatin1(focus->metaObject()->className())));
        }
        cycle.append(focus);
        containers.append(container);
        QTest::keyClick(focus, forward ? Qt::Key_Tab : Qt::Key_Backtab, forward ? Qt::NoModifier : Qt::ShiftModifier);
        QTest::qWait(0); // as between two key presses
    } while (QApplication::focusWidget() != start && cycle.count() < 200);
    QCOMPARE(QApplication::focusWidget(), start);

    for (DolphinViewContainer *container : {m_left, m_right}) {
        // One run of widgets per view ...
        QList<QWidget *> run;
        int runs = 0;
        for (int i = 0; i < cycle.count(); ++i) {
            if (containers.at(i) == container && containers.at((i + cycle.count() - 1) % cycle.count()) != container) {
                ++runs;
                for (int j = i; containers.at(j % cycle.count()) == container; ++j) {
                    run.append(cycle.at(j % cycle.count()));
                }
            }
        }
        QCOMPARE(runs, 1);
        if (!forward) {
            std::reverse(run.begin(), run.end());
        }
        // ... the toolbar first, in the order it is seen ...
        WhaleFileViewToolBar *bar = toolBar(container);
        QVERIFY(bar->isAncestorOf(run.first()));
        int lastX = -1;
        bool inToolBar = true;
        for (QWidget *widget : std::as_const(run)) {
            if (!bar->isAncestorOf(widget)) {
                inToolBar = false;
                continue;
            }
            QVERIFY2(inToolBar, "a widget of the toolbar after one of the view");
            const int x = widget->mapTo(bar, QPoint()).x();
            QVERIFY(x > lastX);
            lastX = x;
        }
        // ... then the file list.
        QVERIFY(std::any_of(run.cbegin(), run.cend(), [container](QWidget *widget) {
            return container->view()->isAncestorOf(widget);
        }));
    }
}

void WhaleLocationBarTest::setLeftShare(double share)
{
    auto splitter = qobject_cast<QSplitter *>(m_left->parentWidget());
    QVERIFY(splitter && splitter->count() == 2);
    // After the animation of a new split view, which sets the sizes as well (deleted when done).
    QTRY_VERIFY(!splitter->findChild<QVariantAnimation *>(QString(), Qt::FindDirectChildrenOnly));
    const QList<int> sizes = splitter->sizes();
    const int total = sizes.at(0) + sizes.at(1);
    splitter->setSizes({int(total * share), total - int(total * share)});
    QCoreApplication::processEvents();
}

void WhaleLocationBarTest::checkWrapped(WhaleFileViewToolBar *bar)
{
    // In the second row: below the buttons, over the whole width, not an item of the row.
    WhaleLocationBar *location = bar->locationBar();
    QVERIFY(bar->isLocationBarWrapped());
    QVERIFY(location->isVisible());
    QVERIFY(!bar->actions().contains(location->action()));
    QCOMPARE(location->parentWidget(), bar);
    QVERIFY(location->geometry().top() >= bar->contentsRect().bottom());
    QVERIFY(location->geometry().bottom() <= bar->rect().bottom());
    QVERIFY(location->width() >= bar->width() - 2 * 8);
    for (QAction *action : bar->actions()) {
        QWidget *widget = bar->widgetForAction(action);
        if (widget && widget->isVisible()) {
            QVERIFY(widget->geometry().bottom() < location->geometry().top());
        }
    }
}

void WhaleLocationBarTest::testWrap()
{
    DolphinViewContainer *container = showMainWindow(1200);
    QVERIFY(container);
    if (auto places = m_mainWindow->findChild<QDockWidget *>(QStringLiteral("placesDock"))) {
        places->hide();
    }
    WhaleFileViewToolBar *bar = toolBar(container);
    WhaleLocationBar *location = bar->locationBar();
    QTRY_VERIFY(location->isVisible());
    QVERIFY(!bar->isLocationBarWrapped());
    QVERIFY(bar->actions().contains(location->action()));
    const int oneRow = bar->height();

    // Narrow: the location bar would get less than 200 px next to the buttons.
    m_mainWindow->resize(300, 700);
    QTRY_VERIFY(bar->isLocationBarWrapped());
    QTRY_VERIFY(bar->height() > oneRow);
    checkWrapped(bar);
    if (QTest::currentTestFailed()) {
        return;
    }
    QCOMPARE(container->urlNavigator(), location->urlNavigator());
    // It works there: navigating, editing.
    container->setUrl(url(QStringLiteral("sub/deeper")));
    QTRY_VERIFY(breadcrumb(location->urlNavigator(), QStringLiteral("deeper")));
    location->urlNavigator()->setUrlEditable(true);
    QTRY_VERIFY(location->urlNavigator()->editor()->isVisible());
    location->urlNavigator()->setUrlEditable(false);

    // Built again (Configure Toolbars) while wrapped: still wrapped where the other buttons
    // leave less than 200 px. With fewer buttons, there is room in the row again.
    writeUserLayout(
        "<Action name=\"go_back\"/><Action name=\"go_forward\"/><Action name=\"go_up\"/><Action name=\"go_home\"/>"
        "<Action name=\"whale_location_bar\"/><Action name=\"view_settings\"/>");
    QTRY_VERIFY(bar->actions().count() == 5);
    QTest::qWait(0);
    QVERIFY(bar->isLocationBarWrapped());
    checkWrapped(bar);
    if (QTest::currentTestFailed()) {
        return;
    }
    writeUserLayout("<Action name=\"go_back\"/><Action name=\"whale_location_bar\"/>");
    QTRY_VERIFY(!bar->isLocationBarWrapped());
    QVERIFY(bar->actions().contains(location->action()));
    QTRY_COMPARE(bar->height(), oneRow);
    writeUserLayout(
        "<Action name=\"go_back\"/><Action name=\"go_forward\"/><Action name=\"go_up\"/><Action name=\"go_home\"/>"
        "<Action name=\"whale_location_bar\"/><Action name=\"view_settings\"/>");
    QTRY_VERIFY(bar->isLocationBarWrapped());
    checkWrapped(bar);
    if (QTest::currentTestFailed()) {
        return;
    }

    // Hidden and shown again.
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(false);
    QVERIFY(!location->isVisible());
    WhaleToolbarSettings::self()->setFileViewToolBarsVisible(true);
    QTRY_VERIFY(location->isVisible());
    QVERIFY(bar->isLocationBarWrapped());

    // Wide again: back into the row, one row high.
    m_mainWindow->resize(1200, 700);
    QTRY_VERIFY(!bar->isLocationBarWrapped());
    QTRY_COMPARE(bar->height(), oneRow);
    QVERIFY(bar->actions().contains(location->action()));
    QVERIFY(location->isVisible());
    QCOMPARE(bar->contentsMargins().bottom(), 0);

    // A layout without the location bar never wraps.
    writeUserLayout("<Action name=\"go_back\"/><Action name=\"go_up\"/>");
    m_mainWindow->resize(300, 700);
    QTest::qWait(50);
    QVERIFY(!bar->isLocationBarWrapped());
    QVERIFY(!location->isVisible());
    QCOMPARE(bar->contentsMargins().bottom(), 0); // one row (lower: only buttons in it)
}

void WhaleLocationBarTest::testWrapSplit()
{
    // One narrow view: the views of the tab wrap together and start at the same height.
    m_mainWindow->resize(1200, 700);
    showSplit();
    if (QTest::currentTestFailed()) {
        return;
    }
    WhaleFileViewToolBar *left = toolBar(m_left);
    WhaleFileViewToolBar *right = toolBar(m_right);
    QTRY_VERIFY(!left->isLocationBarWrapped() && !right->isLocationBarWrapped());
    const int oneRow = left->height();
    setLeftShare(0.85);
    QTRY_VERIFY(right->isLocationBarWrapped());
    QTRY_VERIFY(left->isLocationBarWrapped());
    QTRY_COMPARE(left->height(), right->height());
    QVERIFY(left->height() > oneRow);
    QCOMPARE(m_left->view()->mapTo(m_mainWindow.data(), QPoint()).y(), m_right->view()->mapTo(m_mainWindow.data(), QPoint()).y());
    checkWrapped(left);
    checkWrapped(right);
    if (QTest::currentTestFailed()) {
        return;
    }

    setLeftShare(0.5);
    QTRY_VERIFY(!left->isLocationBarWrapped() && !right->isLocationBarWrapped());
    QTRY_COMPARE(left->height(), oneRow);
    QTRY_COMPARE(right->height(), oneRow);

    // Closing the narrow view: the other one decides alone.
    setLeftShare(0.85);
    QTRY_VERIFY(left->isLocationBarWrapped());
    QPointer<DolphinViewContainer> narrow = m_right;
    m_right->setActive(true);
    QTRY_VERIFY(m_right->isActive());
    global(QStringLiteral("split_view"))->trigger(); // closes the active view
    QTRY_VERIFY(!narrow);
    QTRY_VERIFY(!left->isLocationBarWrapped());
    QTRY_COMPARE(left->height(), oneRow);
}

void WhaleLocationBarTest::testWrapFocusOrder()
{
    // Wrapped: Tab goes through the buttons, then the location bar in the second row, then
    // the file list.
    DolphinViewContainer *container = showMainWindow(1200);
    QVERIFY(container);
    WhaleFileViewToolBar *bar = toolBar(container);
    m_mainWindow->resize(300, 700);
    QTRY_VERIFY(bar->isLocationBarWrapped());
    QTest::qWait(0);
    container->setActive(true);
    container->view()->setFocus();
    QTRY_VERIFY(container->view()->isAncestorOf(QApplication::focusWidget()));
    QWidget *start = QApplication::focusWidget();
    QList<QWidget *> cycle;
    do {
        cycle.append(QApplication::focusWidget());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
        QTest::qWait(0);
    } while (QApplication::focusWidget() != start && cycle.count() < 200);
    QCOMPARE(QApplication::focusWidget(), start);
    // Rotated to the first widget of the toolbar: buttons, location bar, file list.
    const auto first = std::find_if(cycle.begin(), cycle.end(), [bar](QWidget *widget) {
        return bar->isAncestorOf(widget);
    });
    QVERIFY(first != cycle.end());
    std::rotate(cycle.begin(), first, cycle.end());
    int phase = 0; // 0 buttons, 1 location bar, 2 the rest
    for (QWidget *widget : std::as_const(cycle)) {
        const int widgetPhase = bar->locationBar()->isAncestorOf(widget) ? 1 : bar->isAncestorOf(widget) ? 0 : 2;
        QVERIFY2(widgetPhase >= phase, qPrintable(QString::fromLatin1(widget->metaObject()->className())));
        phase = widgetPhase;
    }
    QCOMPARE(phase, 2);
}

QTEST_MAIN(WhaleLocationBarTest)

#include "whalelocationbartest.moc"
