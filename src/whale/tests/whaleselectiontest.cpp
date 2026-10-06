/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleselection.h"
#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewbridge.h"
#include "dolphinviewcontainer.h"
#include "kitemviews/kitemlistcontainer.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whaleview.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KApplicationTrader>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/Paste>
#include <KLocalizedString>
#include <KSycoca>
#include <KUrlMimeData>

#include <QClipboard>
#include <QListView>
#include <QMimeData>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace
{
const QString testListId = QStringLiteral("whale_testlist");

QStringList names(const QList<QUrl> &urls)
{
    QStringList result;
    for (const QUrl &url : urls) {
        result.append(url.fileName());
    }
    return result;
}

QStringList names(const KFileItemList &items)
{
    QStringList result;
    for (const KFileItem &item : items) {
        result.append(item.url().fileName());
    }
    return result;
}

/// What the test list shows as selected, in row order.
QStringList shownSelection(const QListView *listView)
{
    QModelIndexList indexes = listView->selectionModel()->selectedIndexes();
    std::sort(indexes.begin(), indexes.end());
    QStringList result;
    for (const QModelIndex &index : std::as_const(indexes)) {
        result.append(index.data().toString());
    }
    return result;
}

QString shownCurrent(const QListView *listView)
{
    return listView->currentIndex().data().toString();
}

QModelIndex row(const QListView *listView, const QString &name)
{
    const QAbstractItemModel *model = listView->model();
    for (int i = 0; i < model->rowCount(); ++i) {
        if (model->index(i, 0).data().toString() == name) {
            return model->index(i, 0);
        }
    }
    return {};
}

KItemListContainer *itemListContainerOf(DolphinView *view)
{
    return view->findChild<KItemListContainer *>(QString(), Qt::FindDirectChildrenOnly);
}

/// A real double click: QAbstractItemView only reports it after a press on the same item.
void doubleClick(QListView *listView, const QString &name)
{
    const QPoint position = listView->visualRect(row(listView, name)).center();
    QTest::mouseClick(listView->viewport(), Qt::LeftButton, {}, position);
    QTest::mouseDClick(listView->viewport(), Qt::LeftButton, {}, position);
}

void click(QListView *listView, const QString &name, Qt::KeyboardModifiers modifiers = {})
{
    QTest::mouseClick(listView->viewport(), Qt::LeftButton, modifiers, listView->visualRect(row(listView, name)).center());
}
}

class WhaleSelectionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSelectionValue();
    void testDolphinToWhale();
    void testWhaleToDolphin();
    void testCurrentItem();
    void testNoFeedbackLoop();
    void testSelectionSurvivesViewSwitch();
    void testShellFollowsPluginSelection();
    void testSplitSelectionsIndependent();
    void testCreateAndDeleteWhileSelected();
    void testRenameSelected();
    void testFilterHidesSelected();
    void testHiddenFilesWithSelectedHiddenItem();
    void testUrlsOutsideTheListing();
    void testNavigationAndReload();
    void testFileShortcutsSeeMirroredSelection();
    void testActivateFolder();
    void testActivateFiles();

private:
    DolphinViewContainer *showMainWindow();
    QListView *showTestList(DolphinViewContainer *container);
    DolphinViewBridge *bridge(DolphinViewContainer *container) const;
    QUrl url(const QString &name) const;
    QStringList openedFiles() const;

    QTemporaryDir m_tmpHome;
    QString m_openerLog;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    WhaleViewRegistry m_registry;
};

void WhaleSelectionTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    KLocalizedString::setApplicationDomain({"whale"});
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));

    // Opening a file must never start a real application: text files open with a
    // test "application" that only logs the file. Checked before any file is opened.
    m_openerLog = m_tmpHome.filePath(QStringLiteral("opened.log"));
    const QString applications = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
    QVERIFY(QDir().mkpath(applications));
    QFile desktop(applications + QStringLiteral("/whale-test-opener.desktop"));
    QVERIFY(desktop.open(QIODevice::WriteOnly));
    desktop.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Whale Test Opener\nMimeType=text/plain;\n"
                                 "Exec=sh -c 'echo \"$1\" >> %1' sh %f\n")
                      .arg(m_openerLog)
                      .toUtf8());
    desktop.close();
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QVERIFY(QDir().mkpath(config));
    QFile mimeApps(config + QStringLiteral("/mimeapps.list"));
    QVERIFY(mimeApps.open(QIODevice::WriteOnly));
    mimeApps.write("[Default Applications]\ntext/plain=whale-test-opener.desktop\n");
    mimeApps.close();
    KSycoca::self()->ensureCacheValid();
    const KService::Ptr opener = KApplicationTrader::preferredService(QStringLiteral("text/plain"));
    QVERIFY2(opener && opener->desktopEntryName() == QLatin1String("whale-test-opener"), "the test opener must handle text/plain");

    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleSelectionTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(300, 'a'));
    m_dir->createFile(QStringLiteral("b.txt"), QByteArray(100, 'b'));
    m_dir->createFile(QStringLiteral("c.txt"), QByteArray(200, 'c'));
    m_dir->createFile(QStringLiteral(".hidden"));
    m_dir->createFile(QStringLiteral("sub/inner.txt"));
    QFile::remove(m_openerLog);
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleSelectionTest::cleanup()
{
    m_mainWindow.reset();
    m_dir.reset();
}

DolphinViewContainer *WhaleSelectionTest::showMainWindow()
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1200, 800);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    // Whale shares the view properties of all folders by default.
    container->view()->setHiddenFilesShown(false);
    container->view()->setSortRole("text");
    container->view()->setSortOrder(Qt::AscendingOrder);
    container->view()->setSortFoldersFirst(true);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 4;
            },
            5000)) {
        return nullptr;
    }
    return container;
}

QListView *WhaleSelectionTest::showTestList(DolphinViewContainer *container)
{
    WhaleViewPlugin *plugin = m_registry.plugin(testListId);
    if (!plugin || !container->viewHost()->showPluginView(testListId, plugin)) {
        return nullptr;
    }
    return qobject_cast<QListView *>(container->viewHost()->currentView()->widget());
}

DolphinViewBridge *WhaleSelectionTest::bridge(DolphinViewContainer *container) const
{
    return container->viewHost()->bridge();
}

QUrl WhaleSelectionTest::url(const QString &name) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + name);
}

QStringList WhaleSelectionTest::openedFiles() const
{
    QFile log(m_openerLog);
    if (!log.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList files;
    const QStringList lines = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        files.append(QFileInfo(line).fileName());
    }
    return files;
}

void WhaleSelectionTest::testSelectionValue()
{
    const WhaleSelection empty;
    QVERIFY(empty.isEmpty());
    QVERIFY(empty.current().isEmpty());

    // Duplicates are dropped, the order is kept, the current item need not be selected.
    const WhaleSelection selection({url(QStringLiteral("b.txt")), url(QStringLiteral("a.txt")), url(QStringLiteral("b.txt"))}, url(QStringLiteral("c.txt")));
    QCOMPARE(names(selection.selected()), QStringList({QStringLiteral("b.txt"), QStringLiteral("a.txt")}));
    QVERIFY(!selection.isEmpty());
    QVERIFY(selection.contains(url(QStringLiteral("a.txt"))));
    QVERIFY(!selection.contains(url(QStringLiteral("c.txt"))));
    QCOMPARE(selection.current(), url(QStringLiteral("c.txt")));
    QVERIFY(selection != WhaleSelection({url(QStringLiteral("b.txt")), url(QStringLiteral("a.txt"))}));
    QCOMPARE(WhaleSelection(selection.selected(), selection.current()), selection);
}

void WhaleSelectionTest::testDolphinToWhale()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();
    WhaleViewContextImpl context(bridge(container));
    QSignalSpy changed(&context, &WhaleViewContext::selectionChanged);

    // Keyboard selection in the file list: the current item moves, Shift selects.
    view->setFocus();
    KItemListContainer *itemList = itemListContainerOf(view);
    // (The icons view shows all four items in one row. As in Dolphin, moving without
    // Shift selects and sets the anchor; Shift extends from there.)
    QTest::keyClick(itemList, Qt::Key_Right);
    QTRY_COMPARE(names(context.selection().selected()), QStringList({QStringLiteral("a.txt")}));
    QTest::keyClick(itemList, Qt::Key_Right, Qt::ShiftModifier);
    QTRY_COMPARE(names(context.selection().selected()), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QCOMPARE(context.selection().current(), url(QStringLiteral("b.txt")));
    QTRY_VERIFY(!changed.isEmpty());
    QCOMPARE(changed.last().at(0).value<WhaleSelection>(), context.selection());

    // Actions of the shell, e.g. "Select All".
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::SelectAll))->trigger();
    QTRY_COMPARE(context.selection().selected().count(), 4);
    QTRY_COMPARE(changed.last().at(0).value<WhaleSelection>().selected().count(), 4);

    // The test list shows it.
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("sub"), QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}));
    view->clearSelection();
    QTRY_VERIFY(shownSelection(listView).isEmpty());
}

void WhaleSelectionTest::testWhaleToDolphin()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();
    QListView *listView = showTestList(container);
    QVERIFY(listView);

    click(listView, QStringLiteral("a.txt"));
    QTRY_COMPARE(names(view->selectedItems()), QStringList({QStringLiteral("a.txt")}));
    click(listView, QStringLiteral("c.txt"), Qt::ControlModifier);
    QTRY_COMPARE(names(view->selectedItems()), QStringList({QStringLiteral("a.txt"), QStringLiteral("c.txt")}));
    QCOMPARE(bridge(container)->selection().current(), url(QStringLiteral("c.txt")));
    click(listView, QStringLiteral("sub"), Qt::ShiftModifier);
    QTRY_COMPARE(names(view->selectedItems()), QStringList({QStringLiteral("sub"), QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}));

    // Keyboard in the test list.
    QTest::keyClick(listView, Qt::Key_End);
    QTRY_COMPARE(names(view->selectedItems()), QStringList({QStringLiteral("c.txt")}));
    QCOMPARE(bridge(container)->selection().current(), url(QStringLiteral("c.txt")));
}

void WhaleSelectionTest::testCurrentItem()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinViewBridge *viewBridge = bridge(container);

    // Dolphin has a current item without a selection; it need not be selected.
    QVERIFY(viewBridge->selection().isEmpty());
    QVERIFY(!viewBridge->selection().current().isEmpty());
    viewBridge->setSelection(WhaleSelection({url(QStringLiteral("a.txt"))}, url(QStringLiteral("c.txt"))));
    QCOMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral("a.txt")}));
    QCOMPARE(viewBridge->selection().current(), url(QStringLiteral("c.txt")));

    // An empty current URL keeps the current item.
    viewBridge->setSelection(WhaleSelection({url(QStringLiteral("b.txt"))}));
    QCOMPARE(viewBridge->selection().current(), url(QStringLiteral("c.txt")));

    // The test list shows it and keeps it when switching views.
    QListView *listView = showTestList(container);
    QCOMPARE(shownCurrent(listView), QStringLiteral("c.txt"));
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("b.txt")}));
    container->viewHost()->showDolphinView();
    QCOMPARE(viewBridge->selection(), WhaleSelection({url(QStringLiteral("b.txt"))}, url(QStringLiteral("c.txt"))));
}

void WhaleSelectionTest::testNoFeedbackLoop()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    WhaleViewContextImpl context(bridge(container));
    QSignalSpy contextChanged(&context, &WhaleViewContext::selectionChanged);
    QSignalSpy shellChanged(view, &DolphinView::selectionChanged);
    QSignalSpy shownChanged(listView->selectionModel(), &QItemSelectionModel::selectionChanged);

    // A selection set by a view is not reported back to the views ...
    context.setSelection(WhaleSelection({url(QStringLiteral("a.txt")), url(QStringLiteral("b.txt"))}, url(QStringLiteral("b.txt"))));
    // ... but the DolphinView reports it once to the shell, as for its own changes.
    QVERIFY(shellChanged.wait(2000));
    QCOMPARE(names(shellChanged.last().at(0).value<KFileItemList>()), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QVERIFY(!shellChanged.wait(500)); // and nothing more follows
    QCOMPARE(contextChanged.count(), 0);
    QCOMPARE(shellChanged.count(), 1);
    // The test list shows the host's selection: one update, no ping-pong.
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QCOMPARE(shownChanged.count(), 1);

    // Setting the same selection again changes nothing at all.
    context.setSelection(context.selection());
    QVERIFY(!shellChanged.wait(500));
    QCOMPARE(contextChanged.count(), 0);
    QCOMPARE(shownChanged.count(), 1);

    // A user change in the test list: one report to the shell, one to every other
    // view of the area (here the test's own context), no echo into the test list.
    click(listView, QStringLiteral("c.txt"));
    QVERIFY(shellChanged.wait(2000));
    QVERIFY(!shellChanged.wait(500));
    QCOMPARE(shellChanged.count(), 2);
    QCOMPARE(contextChanged.count(), 1);
    QCOMPARE(names(contextChanged.last().at(0).value<WhaleSelection>().selected()), QStringList({QStringLiteral("c.txt")}));
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("c.txt")}));
    QCOMPARE(shownChanged.count(), 2); // the click itself, nothing applied back
}

void WhaleSelectionTest::testSelectionSurvivesViewSwitch()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    DolphinView *view = container->view();

    // Dolphin: several items.
    bridge(container)->setSelection(WhaleSelection({url(QStringLiteral("a.txt")), url(QStringLiteral("c.txt"))}));
    QCOMPARE(names(view->selectedItems()), QStringList({QStringLiteral("a.txt"), QStringLiteral("c.txt")}));

    // Test list: the same items, nothing copied.
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("a.txt"), QStringLiteral("c.txt")}));

    // Changed there ...
    click(listView, QStringLiteral("b.txt"));
    click(listView, QStringLiteral("sub"), Qt::ControlModifier);
    QTRY_COMPARE(names(view->selectedItems()), QStringList({QStringLiteral("sub"), QStringLiteral("b.txt")}));

    // ... and back in Dolphin.
    container->viewHost()->showDolphinView();
    QCOMPARE(names(view->selectedItems()), QStringList({QStringLiteral("sub"), QStringLiteral("b.txt")}));
    QCOMPARE(bridge(container)->selection().current(), url(QStringLiteral("sub")));

    // And again into a new test list instance.
    listView = showTestList(container);
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("sub"), QStringLiteral("b.txt")}));
    QCOMPARE(shownCurrent(listView), QStringLiteral("sub"));
}

void WhaleSelectionTest::testShellFollowsPluginSelection()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    // The information panel gets the main window's selectionChanged().
    QSignalSpy infoPanelInput(m_mainWindow.data(), &DolphinMainWindow::selectionChanged);
    QSignalSpy statusText(container->view(), &DolphinView::statusBarTextChanged);

    click(listView, QStringLiteral("a.txt"));
    click(listView, QStringLiteral("b.txt"), Qt::ControlModifier);
    QTRY_VERIFY(!infoPanelInput.isEmpty() && infoPanelInput.last().at(0).value<KFileItemList>().count() == 2);
    QCOMPARE(names(infoPanelInput.last().at(0).value<KFileItemList>()), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    // "2 files selected (400 B)" in English; the numbers are what matters.
    QTRY_VERIFY(!statusText.isEmpty() && statusText.last().at(0).toString().contains(QLatin1String("2")));
    QVERIFY(statusText.last().at(0).toString().contains(QLatin1String("400")));

    // No stale selection after switching views.
    container->viewHost()->showDolphinView();
    container->view()->clearSelection();
    QTRY_VERIFY(infoPanelInput.last().at(0).value<KFileItemList>().isEmpty());
    listView = showTestList(container);
    QVERIFY(shownSelection(listView).isEmpty());
}

void WhaleSelectionTest::testSplitSelectionsIndependent()
{
    QVERIFY(showMainWindow());
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    DolphinViewContainer *left = page->primaryViewContainer();
    DolphinViewContainer *right = page->secondaryViewContainer();
    QVERIFY(left && right);
    QTRY_COMPARE(right->view()->itemsCount(), 4);
    right->setUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QTRY_COMPARE(right->view()->itemsCount(), 1);

    QListView *leftList = showTestList(left);
    QVERIFY(leftList);
    left->setActive(true);
    click(leftList, QStringLiteral("a.txt"));
    click(leftList, QStringLiteral("c.txt"), Qt::ControlModifier);
    const QUrl inner = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub/inner.txt"));
    bridge(right)->setSelection(WhaleSelection({inner}, inner));
    const WhaleSelection leftSelection({url(QStringLiteral("a.txt")), url(QStringLiteral("c.txt"))}, url(QStringLiteral("c.txt")));
    QTRY_COMPARE(bridge(left)->selection(), leftSelection);
    QCOMPARE(names(bridge(right)->selection().selected()), QStringList({QStringLiteral("inner.txt")}));

    // Activating a half and "Focus Other View" change activity and focus, not selections.
    QAction *focusOtherView = m_mainWindow->actionCollection()->action(QStringLiteral("focus_inactive_split_view"));
    for (int i = 0; i < 3; ++i) {
        focusOtherView->trigger();
        QTRY_VERIFY(left->isActive() != right->isActive());
        QCOMPARE(bridge(left)->selection(), leftSelection);
        QCOMPARE(names(bridge(right)->selection().selected()), QStringList({QStringLiteral("inner.txt")}));
    }
    right->setActive(true);
    QTRY_VERIFY(right->isActive());
    QCOMPARE(shownSelection(leftList), QStringList({QStringLiteral("a.txt"), QStringLiteral("c.txt")}));
    QCOMPARE(bridge(left)->selection(), leftSelection);
}

void WhaleSelectionTest::testCreateAndDeleteWhileSelected()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    DolphinViewBridge *viewBridge = bridge(container);
    viewBridge->setSelection(WhaleSelection({url(QStringLiteral("a.txt")), url(QStringLiteral("b.txt"))}, url(QStringLiteral("b.txt"))));

    // A new item keeps the selection (only indexes shift).
    m_dir->createFile(QStringLiteral("0.txt"));
    QTRY_VERIFY(row(listView, QStringLiteral("0.txt")).isValid());
    QCOMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

    // Deleting a selected item that is also the current one: gone from the selection,
    // and the current item moves to an item that exists.
    QVERIFY(KIO::del(url(QStringLiteral("b.txt")), KIO::HideProgressInfo)->exec());
    QTRY_COMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral("a.txt")}));
    QTRY_COMPARE(shownSelection(listView), QStringList({QStringLiteral("a.txt")}));
    const QUrl current = viewBridge->selection().current();
    QVERIFY(current != url(QStringLiteral("b.txt")));
    QVERIFY(row(listView, current.fileName()).isValid());
    QCOMPARE(shownCurrent(listView), current.fileName());
}

void WhaleSelectionTest::testRenameSelected()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    WhaleViewContextImpl context(bridge(container));
    context.setSelection(WhaleSelection({url(QStringLiteral("a.txt"))}, url(QStringLiteral("a.txt"))));
    QSignalSpy changed(&context, &WhaleViewContext::selectionChanged);

    QVERIFY(KIO::moveAs(url(QStringLiteral("a.txt")), url(QStringLiteral("z.txt")), KIO::HideProgressInfo)->exec());
    // The renamed item stays selected under its new URL; the old URL is gone.
    QTRY_COMPARE(context.selection(), WhaleSelection({url(QStringLiteral("z.txt"))}, url(QStringLiteral("z.txt"))));
    QVERIFY(!changed.isEmpty());
    QTRY_COMPARE(shownSelection(listView), QStringList({QStringLiteral("z.txt")}));
}

void WhaleSelectionTest::testFilterHidesSelected()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    DolphinViewBridge *viewBridge = bridge(container);
    viewBridge->setSelection(WhaleSelection({url(QStringLiteral("a.txt")), url(QStringLiteral("b.txt"))}));

    // Filtered items leave the listing and so the selection, as in Dolphin.
    container->view()->setNameFilter(QStringLiteral("b."));
    QTRY_COMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral("b.txt")}));
    QTRY_COMPARE(shownSelection(listView), QStringList({QStringLiteral("b.txt")}));
    QVERIFY(!viewBridge->selection().current().isEmpty());
    QVERIFY(row(listView, viewBridge->selection().current().fileName()).isValid());

    // They do not come back selected.
    container->view()->setNameFilter(QString());
    QTRY_COMPARE(listView->model()->rowCount(), 4);
    QCOMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral("b.txt")}));
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("b.txt")}));
}

void WhaleSelectionTest::testHiddenFilesWithSelectedHiddenItem()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    DolphinViewBridge *viewBridge = bridge(container);
    container->view()->setHiddenFilesShown(true);
    QTRY_VERIFY(row(listView, QStringLiteral(".hidden")).isValid());

    click(listView, QStringLiteral(".hidden"));
    click(listView, QStringLiteral("a.txt"), Qt::ControlModifier);
    QTRY_COMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral(".hidden"), QStringLiteral("a.txt")}));

    container->view()->setHiddenFilesShown(false);
    QTRY_COMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral("a.txt")}));
    QTRY_COMPARE(shownSelection(listView), QStringList({QStringLiteral("a.txt")}));
    QCOMPARE(names(container->view()->selectedItems()), QStringList({QStringLiteral("a.txt")}));
}

void WhaleSelectionTest::testUrlsOutsideTheListing()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    WhaleViewContextImpl context(bridge(container));
    QSignalSpy changed(&context, &WhaleViewContext::selectionChanged);
    const QUrl before = context.selection().current();

    // Deep or unknown URLs are ignored (no deep selection before the treemap work).
    const QUrl inner = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub/inner.txt"));
    const QUrl missing = url(QStringLiteral("missing.txt"));
    context.setSelection(WhaleSelection({inner, url(QStringLiteral("a.txt")), missing, url(QStringLiteral("a.txt"))}, missing));
    QCOMPARE(context.selection(), WhaleSelection({url(QStringLiteral("a.txt"))}, before));
    // The result differs from what was asked for, so the views hear about it.
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(changed.last().at(0).value<WhaleSelection>(), context.selection());
    QCOMPARE(shownSelection(listView), QStringList({QStringLiteral("a.txt")}));

    // Activating unknown URLs does nothing.
    QSignalSpy activated(container->view(), &DolphinView::itemActivated);
    context.activateItems({inner, missing});
    QCoreApplication::processEvents();
    QCOMPARE(activated.count(), 0);
    QCOMPARE(container->url().adjusted(QUrl::StripTrailingSlash), m_dir->url().adjusted(QUrl::StripTrailingSlash));
}

void WhaleSelectionTest::testNavigationAndReload()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    DolphinViewBridge *viewBridge = bridge(container);
    viewBridge->setSelection(WhaleSelection({url(QStringLiteral("b.txt"))}, url(QStringLiteral("b.txt"))));

    // Reloading (F5) keeps the selection: Dolphin restores it after loading.
    QSignalSpy loaded(container->view(), &DolphinView::directoryLoadingCompleted);
    container->view()->reload();
    QVERIFY(loaded.wait(5000));
    QTRY_COMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral("b.txt")}));
    QTRY_COMPARE(shownSelection(listView), QStringList({QStringLiteral("b.txt")}));

    // Navigating replaces the listing and empties the selection.
    container->setUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QVERIFY(loaded.wait(5000));
    QTRY_VERIFY(viewBridge->selection().isEmpty());
    QTRY_VERIFY(shownSelection(listView).isEmpty());

    // Selecting while a folder is loading: only items already listed can be selected.
    container->setUrl(m_dir->url());
    viewBridge->setSelection(WhaleSelection({url(QStringLiteral("a.txt"))}));
    QVERIFY(loaded.wait(5000));
    QVERIFY(viewBridge->selection().selected().count() <= 1);
    viewBridge->setSelection(WhaleSelection({url(QStringLiteral("a.txt"))}));
    QCOMPARE(names(viewBridge->selection().selected()), QStringList({QStringLiteral("a.txt")}));
    QTRY_COMPARE(shownSelection(listView), QStringList({QStringLiteral("a.txt")}));
}

void WhaleSelectionTest::testFileShortcutsSeeMirroredSelection()
{
    // Not the command routing of M5: the shell's file actions still act on the
    // DolphinView. Since the selection is mirrored, that is now the selection the
    // test list shows. Only checked with actions that change no files.
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        QSKIP("uses the clipboard, which is the user's own outside of the offscreen platform");
    }
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    click(listView, QStringLiteral("a.txt"));
    click(listView, QStringLiteral("b.txt"), Qt::ControlModifier);
    QTRY_COMPARE(names(container->view()->selectedItems()), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

    auto clipboardUrls = []() {
        return names(KUrlMimeData::urlsFromMimeData(QApplication::clipboard()->mimeData()));
    };
    QTest::keyClick(listView, Qt::Key_C, Qt::ControlModifier);
    QTRY_COMPARE(clipboardUrls(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QVERIFY(!KIO::isClipboardDataCut(QApplication::clipboard()->mimeData()));

    click(listView, QStringLiteral("c.txt"));
    QTRY_COMPARE(names(container->view()->selectedItems()), QStringList({QStringLiteral("c.txt")}));
    QTest::keyClick(listView, Qt::Key_X, Qt::ControlModifier);
    QTRY_COMPARE(clipboardUrls(), QStringList({QStringLiteral("c.txt")}));
    QVERIFY(KIO::isClipboardDataCut(QApplication::clipboard()->mimeData()));
    QApplication::clipboard()->clear();

    // Move to Trash would trash exactly these (not triggered here).
    QAction *trash = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::MoveToTrash));
    QVERIFY(trash && trash->isEnabled());
}

void WhaleSelectionTest::testActivateFolder()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    QVERIFY(listView);

    // Double click on a folder: the container's own handling enters it; the view stays.
    doubleClick(listView, QStringLiteral("sub"));
    QTRY_COMPARE(container->url(), QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QCOMPARE(container->viewHost()->currentViewId(), testListId);
    QTRY_VERIFY(row(listView, QStringLiteral("inner.txt")).isValid());

    // Back, then Enter on the selected folder.
    m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Back))->trigger();
    QTRY_VERIFY(row(listView, QStringLiteral("sub")).isValid());
    click(listView, QStringLiteral("sub"));
    QTest::keyClick(listView, Qt::Key_Return);
    QTRY_COMPARE(container->url(), QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
}

void WhaleSelectionTest::testActivateFiles()
{
    QVERIFY2(KApplicationTrader::preferredService(QStringLiteral("text/plain"))->desktopEntryName() == QLatin1String("whale-test-opener"),
             "refusing to open files without the test opener");
    DolphinViewContainer *container = showMainWindow();
    QVERIFY(container);
    QListView *listView = showTestList(container);
    QVERIFY(listView);

    // Double click on a file: opened through the container's usual path (KIO::OpenUrlJob).
    doubleClick(listView, QStringLiteral("a.txt"));
    QTRY_COMPARE_WITH_TIMEOUT(openedFiles(), QStringList({QStringLiteral("a.txt")}), 10000);
    QCOMPARE(container->url().adjusted(QUrl::StripTrailingSlash), m_dir->url().adjusted(QUrl::StripTrailingSlash));

    // Enter with two selected files opens both (the DolphinView's multi-item handling).
    click(listView, QStringLiteral("b.txt"));
    click(listView, QStringLiteral("c.txt"), Qt::ControlModifier);
    QTest::keyClick(listView, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(openedFiles().count() >= 2, 10000);
    auto opened = [this]() {
        QStringList files = openedFiles();
        files.sort();
        return files;
    };
    QTRY_COMPARE_WITH_TIMEOUT(opened(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}), 10000);
}

QTEST_MAIN(WhaleSelectionTest)

#include "whaleselectiontest.moc"
