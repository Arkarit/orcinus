/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config-dolphin.h"
#include "dolphin_generalsettings.h"
#include "dolphinmainwindow.h"
#include "dolphintabpage.h"
#include "dolphintabwidget.h"
#include "dolphinviewbridge.h"
#include "dolphinviewcontainer.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "testdir.h"
#include "testhelpers.h"
#include "views/dolphinview.h"
#include "whalefileoperations.h"
#include "whalefileoperationsimpl.h"
#include "whaleview.h"
#include "whaleviewactions.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KUrlMimeData>

#include <QApplication>
#include <QDrag>
#include <QGraphicsView>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace
{
const QString testListId = QStringLiteral("whale_testlist");

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

QStringList names(const QList<QUrl> &urls)
{
    QStringList result;
    for (const QUrl &url : urls) {
        result.append(url.fileName());
    }
    return result;
}

QStringList selectedNames(DolphinView *view)
{
    QStringList result;
    const KFileItemList items = view->selectedItems();
    for (const KFileItem &item : items) {
        result.append(item.text());
    }
    result.sort();
    return result;
}

KItemSet indexSet(std::initializer_list<int> indexes)
{
    KItemSet result;
    for (int index : indexes) {
        result.insert(index);
    }
    return result;
}

/// A point of the list view's background, below its rows.
QPoint background(const QListView *listView)
{
    return QPoint(listView->viewport()->width() / 2, listView->viewport()->height() - 5);
}

/**
 * The events a widget gets from a drag that enters it at @p position, moves and drops
 * there, with @p modifiers held. Only the transport differs from a real drag; the
 * widget's own handling runs.
 * @return The drop action of the move event; Qt::IgnoreAction if the widget did not
 *         accept it (then there is no drop).
 */
Qt::DropAction dragAndDrop(QWidget *target, const QPoint &position, const QMimeData *data, Qt::KeyboardModifiers modifiers)
{
    const Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
    QDragEnterEvent enter(position, actions, data, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(target, &enter);
    QDragMoveEvent move(position, actions, data, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(target, &move);
    if (!move.isAccepted() || move.dropAction() == Qt::IgnoreAction) {
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(target, &leave);
        return Qt::IgnoreAction;
    }
    QDropEvent drop(position, actions, data, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(target, &drop);
    return drop.isAccepted() ? move.dropAction() : Qt::IgnoreAction;
}

/// A mouse event of the left button for @p widget. Sent directly, so no state of the
/// application's pointer is left behind when a drag takes the release.
void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &position)
{
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, position, widget->mapToGlobal(position), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

int dragCount = 0;

/// Runs the drags of plugin views with @p runner while it exists.
class DragRunner
{
public:
    explicit DragRunner(const WhaleFileOperationsImpl::DragRunner &runner)
    {
        WhaleFileOperationsImpl::setDragRunner([runner](QDrag *drag) {
            ++dragCount;
            return runner(drag);
        });
    }
    ~DragRunner()
    {
        WhaleFileOperationsImpl::setDragRunner({});
    }
};

/**
 * The user's drag gesture on the row @p name of the test list: press, then move beyond
 * the drag distance; the test list starts its drag. A running drag takes the release of
 * the button (QDrag::exec()), so the view only gets it if no drag started.
 */
void dragGesture(QListView *listView, const QString &name)
{
    QWidget *viewport = listView->viewport();
    const QPoint start = listView->visualRect(row(listView, name)).center();
    const int drags = dragCount;
    sendMouse(viewport, QEvent::MouseButtonPress, start);
    // The drag starts on one of the moves and owns the pointer from then on: no further
    // move and no release reach the view.
    const QPoint step(QApplication::startDragDistance() + 2, 0);
    for (const QPoint &position : {start + QPoint(1, 0), start + step, start + step * 2}) {
        sendMouse(viewport, QEvent::MouseMove, position);
        if (dragCount != drags) {
            return;
        }
    }
    sendMouse(viewport, QEvent::MouseButtonRelease, start + step * 2);
}

/// The widget of the file list (DolphinView) that gets mouse and drag events.
QWidget *fileListEventWidget(DolphinView *view)
{
    return qobject_cast<QGraphicsView *>(view->itemListContainer()->viewport())->viewport();
}
}

/**
 * M7: drag and drop of plugin views (the test list) through WhaleFileOperations, with
 * real file operations in temporary folders. Only the transport of a drag (QDrag::exec(),
 * which needs a real pointer) is replaced by delivering the drag events to the target.
 */
class WhaleDragDropTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testDragDataOfOneItem();
    void testDragDataOfSeveralItems();
    void testDropTargetRules();
    void testDropCopy();
    void testDropMove();
    void testDropOntoFolder();
    void testDropMenu();
    void testSplitTestListToTestList();
    void testSplitTestListToFileList();
    void testSplitFileListToTestList();
    void testSelectionAndFocusAfterDrop();

private:
    DolphinViewContainer *showMainWindow();
    /// Split view: m_dir on the left, m_target on the right.
    std::pair<DolphinViewContainer *, DolphinViewContainer *> showSplitView();
    static QString listingState(DolphinViewContainer *container);
    QListView *showTestList(DolphinViewContainer *container);
    QWidget *focusedPluginWidget() const;
    QUrl url(const QString &name) const;
    QString targetPath(const QString &name) const;

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<TestDir> m_target;
    QScopedPointer<TestDir> m_external;
    QScopedPointer<DolphinMainWindow> m_mainWindow;
    // Why showMainWindow() or showSplitView() gave up, with the state at that moment.
    QString m_setupFailure;
    WhaleViewRegistry m_registry;
};

void WhaleDragDropTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    QCoreApplication::setApplicationName(QStringLiteral(WHALE_COMPONENT));
    QCoreApplication::setOrganizationDomain(QStringLiteral(WHALE_ORG_DOMAIN));
    qputenv("HOME", m_tmpHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    TestHelpers::disableAnimations();
    TestHelpers::openNewWindowsInSingleView();
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_VIEW_PLUGIN_DIR));
    GeneralSettings::self()->setRememberOpenedTabs(false);
    GeneralSettings::self()->save();
}

void WhaleDragDropTest::init()
{
    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), "a");
    m_dir->createFile(QStringLiteral("b.txt"), "b");
    m_dir->createFile(QStringLiteral("c.txt"), "c");
    m_dir->createFile(QStringLiteral("sub/inner.txt"), "inner");
    m_target.reset(new TestDir());
    m_target->createFile(QStringLiteral("existing.txt"), "existing");
    m_external.reset(new TestDir());
    m_external->createFile(QStringLiteral("x.txt"), "x");
    m_external->createFile(QStringLiteral("y.txt"), "y");
    m_mainWindow.reset(new DolphinMainWindow());
}

void WhaleDragDropTest::cleanup()
{
    WhaleFileOperationsImpl::setDragRunner({});
    m_mainWindow.reset();
    m_dir.reset();
    m_target.reset();
    m_external.reset();
}

DolphinViewContainer *WhaleDragDropTest::showMainWindow()
{
    m_mainWindow->openDirectories({m_dir->url()}, false);
    m_mainWindow->resize(1000, 600);
    m_mainWindow->show();
    if (!QTest::qWaitForWindowActive(m_mainWindow.data())) {
        const QWidget *active = QApplication::activeWindow();
        m_setupFailure =
            QStringLiteral("main window not active (visible %1, active window %2)")
                .arg(m_mainWindow->isVisible())
                .arg(active ? QString::fromLatin1(active->metaObject()->className()) + QLatin1Char(' ') + active->objectName() : QStringLiteral("none"));
        return nullptr;
    }
    DolphinViewContainer *container = m_mainWindow->activeViewContainer();
    container->view()->setHiddenFilesShown(false);
    container->view()->setSortRole("text");
    container->view()->setSortOrder(Qt::AscendingOrder);
    container->view()->setSortFoldersFirst(true);
    container->view()->setViewMode(DolphinView::IconsView);
    if (!QTest::qWaitFor(
            [container]() {
                return container->view()->itemsCount() == 4;
            },
            5000)) {
        m_setupFailure = QStringLiteral("left view: ") + listingState(container);
        return nullptr;
    }
    return container;
}

QString WhaleDragDropTest::listingState(DolphinViewContainer *container)
{
    QStringList names;
    const KFileItemList items = container->view()->items();
    for (const KFileItem &item : items) {
        names.append(item.name());
    }
    return QStringLiteral("%1 items [%2] in %3 (view at %4, name filter \"%5\", hidden files %6)")
        .arg(container->view()->itemsCount())
        .arg(names.join(QLatin1Char(',')), container->url().toDisplayString(), container->view()->url().toDisplayString(), container->view()->nameFilter())
        .arg(container->view()->hiddenFilesShown());
}

std::pair<DolphinViewContainer *, DolphinViewContainer *> WhaleDragDropTest::showSplitView()
{
    DolphinViewContainer *left = showMainWindow();
    if (!left) {
        return {};
    }
    m_mainWindow->actionCollection()->action(QStringLiteral("split_view"))->trigger();
    DolphinTabPage *page = m_mainWindow->findChild<DolphinTabWidget *>(QStringLiteral("tabWidget"))->currentTabPage();
    DolphinViewContainer *right = page->secondaryViewContainer();
    right->setUrl(m_target->url());
    if (!QTest::qWaitFor(
            [right]() {
                return right->view()->itemsCount() == 1;
            },
            5000)) {
        m_setupFailure = QStringLiteral("right view: ") + listingState(right);
        return {};
    }
    return {left, right};
}

QListView *WhaleDragDropTest::showTestList(DolphinViewContainer *container)
{
    WhaleViewPlugin *plugin = m_registry.plugin(testListId);
    if (!plugin || !container->viewHost()->showPluginView(testListId, plugin)) {
        return nullptr;
    }
    return qobject_cast<QListView *>(container->viewHost()->currentView()->widget());
}

QUrl WhaleDragDropTest::url(const QString &name) const
{
    return QUrl::fromLocalFile(m_dir->path() + QLatin1Char('/') + name);
}

QString WhaleDragDropTest::targetPath(const QString &name) const
{
    return m_target->path() + QLatin1Char('/') + name;
}

void WhaleDragDropTest::testDragDataOfOneItem()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY2(container, qPrintable(m_setupFailure));
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    container->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("b.txt"))}, url(QStringLiteral("b.txt"))));
    QTRY_COMPARE(listView->selectionModel()->selectedIndexes().count(), 1);

    int drags = 0;
    QStringList formats;
    QList<QUrl> urls;
    QList<QUrl> kdeUrls;
    QPixmap pixmap;
    DragRunner runner([&](QDrag *drag) {
        ++drags;
        formats = drag->mimeData()->formats();
        urls = drag->mimeData()->urls();
        kdeUrls = KUrlMimeData::urlsFromMimeData(drag->mimeData(), KUrlMimeData::PreferKdeUrls);
        pixmap = drag->pixmap();
        return Qt::IgnoreAction;
    });
    dragGesture(listView, QStringLiteral("b.txt"));
    QCOMPARE(drags, 1);
    QCOMPARE(urls, QList<QUrl>({url(QStringLiteral("b.txt"))}));
    QCOMPARE(kdeUrls, QList<QUrl>({url(QStringLiteral("b.txt"))}));
    QVERIFY(!pixmap.isNull());

    // The same data as a drag of the file list (KItemListController::startDragging()).
    auto model = qobject_cast<KFileItemModel *>(container->view()->itemListContainer()->controller()->model());
    std::unique_ptr<QMimeData> dolphinData(model->createMimeData(indexSet({model->index(url(QStringLiteral("b.txt")))})));
    KUrlMimeData::exportUrlsToPortal(dolphinData.get());
    QCOMPARE(formats, dolphinData->formats());

    // Dragging does not change the selection. A drag from a row that is not selected
    // selects it first (the press does), as in the file list.
    QCOMPARE(selectedNames(container->view()), QStringList({QStringLiteral("b.txt")}));
    dragGesture(listView, QStringLiteral("a.txt"));
    QCOMPARE(drags, 2);
    QCOMPARE(urls, QList<QUrl>({url(QStringLiteral("a.txt"))}));
    QTRY_COMPARE(selectedNames(container->view()), QStringList({QStringLiteral("a.txt")}));
}

void WhaleDragDropTest::testDragDataOfSeveralItems()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY2(container, qPrintable(m_setupFailure));
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    const QList<QUrl> selection = {url(QStringLiteral("c.txt")), url(QStringLiteral("sub")), url(QStringLiteral("a.txt"))};
    container->viewHost()->bridge()->setSelection(WhaleSelection(selection, url(QStringLiteral("a.txt"))));
    QTRY_COMPARE(listView->selectionModel()->selectedIndexes().count(), 3);

    QList<QUrl> urls;
    DragRunner runner([&](QDrag *drag) {
        urls = KUrlMimeData::urlsFromMimeData(drag->mimeData(), KUrlMimeData::PreferKdeUrls);
        return Qt::IgnoreAction;
    });
    // Dragging any selected row drags the whole selection, in the host's order.
    dragGesture(listView, QStringLiteral("c.txt"));
    QCOMPARE(names(urls), QStringList({QStringLiteral("sub"), QStringLiteral("a.txt"), QStringLiteral("c.txt")}));

    // URLs outside the listing are no part of a drag.
    WhaleFileOperations *operations = container->viewHost()->pluginContext()->fileOperations();
    urls.clear();
    operations->startDrag({url(QStringLiteral("sub/inner.txt")), url(QStringLiteral("b.txt"))});
    QCOMPARE(names(urls), QStringList({QStringLiteral("b.txt")}));
    urls = {QUrl(QStringLiteral("file:///nothing"))};
    QCOMPARE(operations->startDrag({url(QStringLiteral("sub/inner.txt"))}), Qt::IgnoreAction);
    QCOMPARE(names(urls), QStringList({QStringLiteral("nothing")}));
}

void WhaleDragDropTest::testDropTargetRules()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY2(container, qPrintable(m_setupFailure));
    m_dir->createDir(QStringLiteral("readonly"));
    QFile::setPermissions(m_dir->path() + QStringLiteral("/readonly"), QFile::ReadOwner | QFile::ExeOwner);
    container->reload();
    QTRY_COMPARE(container->view()->itemsCount(), 5);
    QListView *listView = showTestList(container);
    QVERIFY(listView);
    WhaleFileOperations *operations = container->viewHost()->pluginContext()->fileOperations();

    auto accepts = [operations](const QList<QUrl> &dragged, const QUrl &target) {
        QMimeData data;
        KUrlMimeData::setUrls(dragged, dragged, &data);
        QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, &data, Qt::LeftButton, Qt::NoModifier);
        const bool accepted = operations->updateDropAction(&enter, target);
        // The event says the same as the result.
        return accepted && enter.isAccepted() && enter.dropAction() != Qt::IgnoreAction;
    };
    const QList<QUrl> external = {QUrl::fromLocalFile(m_external->path() + QStringLiteral("/x.txt"))};
    // The location (background) and a writable folder take drops.
    QVERIFY(accepts(external, QUrl()));
    QVERIFY(accepts(external, url(QStringLiteral("sub"))));
    // A plain file does not (as in the file list), a read-only folder neither.
    QVERIFY(!accepts(external, url(QStringLiteral("a.txt"))));
    QVERIFY(!accepts(external, url(QStringLiteral("readonly"))));
    // A folder never onto itself.
    QVERIFY(!accepts({url(QStringLiteral("sub"))}, url(QStringLiteral("sub"))));
    // Items outside the listing are no target.
    QVERIFY(!accepts(external, url(QStringLiteral("sub/inner.txt"))));
    QMimeData data;
    QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &data, Qt::LeftButton, Qt::ControlModifier);
    QVERIFY(!operations->drop(&drop, url(QStringLiteral("sub/inner.txt")), listView->viewport()));
    QFile::setPermissions(m_dir->path() + QStringLiteral("/readonly"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

void WhaleDragDropTest::testDropCopy()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY2(container, qPrintable(m_setupFailure));
    QListView *listView = showTestList(container);
    QVERIFY(listView);

    // A drag of another application onto the background: copied into the location.
    QMimeData data;
    const QList<QUrl> dragged = {QUrl::fromLocalFile(m_external->path() + QStringLiteral("/x.txt")),
                                 QUrl::fromLocalFile(m_external->path() + QStringLiteral("/y.txt"))};
    KUrlMimeData::setUrls(dragged, dragged, &data);
    QCOMPARE(dragAndDrop(listView->viewport(), background(listView), &data, Qt::ControlModifier), Qt::CopyAction);
    QTRY_VERIFY(QFile::exists(m_dir->path() + QStringLiteral("/x.txt")) && QFile::exists(m_dir->path() + QStringLiteral("/y.txt")));
    QVERIFY(QFile::exists(m_external->path() + QStringLiteral("/x.txt")));
    QVERIFY(QFile::exists(m_external->path() + QStringLiteral("/y.txt")));
    QTRY_VERIFY(row(listView, QStringLiteral("x.txt")).isValid());
}

void WhaleDragDropTest::testDropMove()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY2(container, qPrintable(m_setupFailure));
    QListView *listView = showTestList(container);
    QVERIFY(listView);

    QMimeData data;
    const QList<QUrl> dragged = {QUrl::fromLocalFile(m_external->path() + QStringLiteral("/x.txt"))};
    KUrlMimeData::setUrls(dragged, dragged, &data);
    QCOMPARE(dragAndDrop(listView->viewport(), background(listView), &data, Qt::ShiftModifier), Qt::MoveAction);
    QTRY_VERIFY(QFile::exists(m_dir->path() + QStringLiteral("/x.txt")));
    QTRY_VERIFY(!QFile::exists(m_external->path() + QStringLiteral("/x.txt")));
    QVERIFY(QFile::exists(m_external->path() + QStringLiteral("/y.txt")));
}

void WhaleDragDropTest::testDropOntoFolder()
{
    DolphinViewContainer *container = showMainWindow();
    QVERIFY2(container, qPrintable(m_setupFailure));
    QListView *listView = showTestList(container);
    QVERIFY(listView);

    // The test list's own items onto its folder row: moved into the folder.
    container->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("a.txt")), url(QStringLiteral("b.txt"))}, url(QStringLiteral("a.txt"))));
    QTRY_COMPARE(listView->selectionModel()->selectedIndexes().count(), 2);
    Qt::DropAction result = Qt::IgnoreAction;
    DragRunner runner([&](QDrag *drag) {
        result = dragAndDrop(listView->viewport(), listView->visualRect(row(listView, QStringLiteral("sub"))).center(), drag->mimeData(), Qt::ShiftModifier);
        return result;
    });
    dragGesture(listView, QStringLiteral("a.txt"));
    QCOMPARE(result, Qt::MoveAction);
    QTRY_VERIFY(QFile::exists(m_dir->path() + QStringLiteral("/sub/a.txt")) && QFile::exists(m_dir->path() + QStringLiteral("/sub/b.txt")));
    QTRY_VERIFY(!QFile::exists(url(QStringLiteral("a.txt")).toLocalFile()) && !QFile::exists(url(QStringLiteral("b.txt")).toLocalFile()));
    QVERIFY(QFile::exists(url(QStringLiteral("c.txt")).toLocalFile()));
    QTRY_VERIFY(!row(listView, QStringLiteral("a.txt")).isValid());
    // The location stays.
    QCOMPARE(container->url(), m_dir->url());
}

void WhaleDragDropTest::testDropMenu()
{
    if (QGuiApplication::platformName() == QLatin1String("wayland")) {
        QSKIP("Wayland shows popup menus only in answer to real input");
    }
    DolphinViewContainer *container = showMainWindow();
    QVERIFY2(container, qPrintable(m_setupFailure));
    QListView *listView = showTestList(container);
    QVERIFY(listView);

    // Without modifier keys, KDE's drop menu asks, at the drop position in the test list.
    QMimeData data;
    const QList<QUrl> dragged = {QUrl::fromLocalFile(m_external->path() + QStringLiteral("/x.txt"))};
    KUrlMimeData::setUrls(dragged, dragged, &data);
    const QPoint position = background(listView);
    QVERIFY(dragAndDrop(listView->viewport(), position, &data, Qt::NoModifier) != Qt::IgnoreAction);
    QMenu *menu = nullptr;
    QTRY_VERIFY((menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) && menu->isVisible());
    QStringList entries;
    QAction *link = nullptr;
    const auto actions = menu->actions();
    for (QAction *action : actions) {
        if (!action->isSeparator()) {
            entries.append(action->text().remove(QLatin1Char('&')));
        }
        if (action->text().remove(QLatin1Char('&')).startsWith(QStringLiteral("Link Here"))) {
            link = action;
        }
    }
    QVERIFY2(entries.join(QLatin1Char(',')).contains(QStringLiteral("Move Here")), qPrintable(entries.join(QLatin1Char(','))));
    QVERIFY2(entries.join(QLatin1Char(',')).contains(QStringLiteral("Copy Here")), qPrintable(entries.join(QLatin1Char(','))));
    QVERIFY2(link, qPrintable(entries.join(QLatin1Char(','))));
    QVERIFY((menu->geometry().topLeft() - listView->viewport()->mapToGlobal(position)).manhattanLength() < 50);
    link->trigger();
    menu->close();
    QTRY_VERIFY(QFileInfo(m_dir->path() + QStringLiteral("/x.txt")).isSymLink());
    QVERIFY(QFile::exists(m_external->path() + QStringLiteral("/x.txt")));
}

void WhaleDragDropTest::testSplitTestListToTestList()
{
    auto [left, right] = showSplitView();
    QVERIFY2(left && right, qPrintable(m_setupFailure));
    QListView *leftList = showTestList(left);
    QListView *rightList = showTestList(right);
    QVERIFY(leftList && rightList);
    left->setActive(true);
    left->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("a.txt"))}, url(QStringLiteral("a.txt"))));
    right->viewHost()->bridge()->setSelection(WhaleSelection({QUrl::fromLocalFile(targetPath(QStringLiteral("existing.txt")))}, {}));
    QTRY_COMPARE(leftList->selectionModel()->selectedIndexes().count(), 1);

    DragRunner runner([&](QDrag *drag) {
        return dragAndDrop(rightList->viewport(), background(rightList), drag->mimeData(), Qt::ControlModifier);
    });
    dragGesture(leftList, QStringLiteral("a.txt"));
    // Copied into the right half's location only.
    QTRY_VERIFY(QFile::exists(targetPath(QStringLiteral("a.txt"))));
    QVERIFY(QFile::exists(url(QStringLiteral("a.txt")).toLocalFile()));
    QTRY_VERIFY(row(rightList, QStringLiteral("a.txt")).isValid());
    QCOMPARE(left->view()->itemsCount(), 4);
    // The target half is active, as after a drop onto the file list.
    QTRY_VERIFY(right->isActive());
    QVERIFY(!left->isActive());
    QCOMPARE(selectedNames(left->view()), QStringList({QStringLiteral("a.txt")}));
}

void WhaleDragDropTest::testSplitTestListToFileList()
{
    auto [left, right] = showSplitView();
    QVERIFY2(left && right, qPrintable(m_setupFailure));
    QListView *leftList = showTestList(left);
    QVERIFY(leftList);
    left->setActive(true);
    left->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("b.txt")), url(QStringLiteral("c.txt"))}, url(QStringLiteral("b.txt"))));
    QTRY_COMPARE(leftList->selectionModel()->selectedIndexes().count(), 2);

    // The file list of the right half gets the drag on its own item view.
    QWidget *fileListViewport = fileListEventWidget(right->view());
    const QPoint emptyArea(fileListViewport->width() - 20, fileListViewport->height() - 20);
    Qt::DropAction result = Qt::IgnoreAction;
    DragRunner runner([&](QDrag *drag) {
        result = dragAndDrop(fileListViewport, emptyArea, drag->mimeData(), Qt::ShiftModifier);
        return result;
    });
    dragGesture(leftList, QStringLiteral("c.txt"));
    QCOMPARE(result, Qt::MoveAction);
    QTRY_VERIFY(QFile::exists(targetPath(QStringLiteral("b.txt"))) && QFile::exists(targetPath(QStringLiteral("c.txt"))));
    QTRY_VERIFY(!QFile::exists(url(QStringLiteral("b.txt")).toLocalFile()) && !QFile::exists(url(QStringLiteral("c.txt")).toLocalFile()));
    QTRY_COMPARE(right->view()->itemsCount(), 3);
    QTRY_VERIFY(!row(leftList, QStringLiteral("b.txt")).isValid());
    QTRY_VERIFY(right->isActive());
    // The moved items left the source's selection.
    QTRY_COMPARE(selectedNames(left->view()), QStringList());
}

void WhaleDragDropTest::testSplitFileListToTestList()
{
    auto [left, right] = showSplitView();
    QVERIFY2(left && right, qPrintable(m_setupFailure));
    QListView *rightList = showTestList(right);
    QVERIFY(rightList);
    left->setActive(true);

    // The data of a drag of the left file list (KItemListController::startDragging()).
    auto model = qobject_cast<KFileItemModel *>(left->view()->itemListContainer()->controller()->model());
    std::unique_ptr<QMimeData> data(model->createMimeData(indexSet({model->index(url(QStringLiteral("a.txt"))), model->index(url(QStringLiteral("sub")))})));
    KUrlMimeData::exportUrlsToPortal(data.get());
    QCOMPARE(dragAndDrop(rightList->viewport(), background(rightList), data.get(), Qt::ControlModifier), Qt::CopyAction);
    QTRY_VERIFY(QFile::exists(targetPath(QStringLiteral("a.txt"))) && QFile::exists(targetPath(QStringLiteral("sub/inner.txt"))));
    QVERIFY(QFile::exists(url(QStringLiteral("a.txt")).toLocalFile()));
    QTRY_VERIFY(row(rightList, QStringLiteral("sub")).isValid());
    QTRY_VERIFY(right->isActive());
}

void WhaleDragDropTest::testSelectionAndFocusAfterDrop()
{
    auto [left, right] = showSplitView();
    QVERIFY2(left && right, qPrintable(m_setupFailure));
    QListView *leftList = showTestList(left);
    QListView *rightList = showTestList(right);
    QVERIFY(leftList && rightList);
    leftList->setFocus();
    QTRY_VERIFY(left->isActive());
    left->viewHost()->bridge()->setSelection(WhaleSelection({url(QStringLiteral("a.txt")), url(QStringLiteral("c.txt"))}, url(QStringLiteral("a.txt"))));
    QTRY_COMPARE(leftList->selectionModel()->selectedIndexes().count(), 2);
    auto viewActions = m_mainWindow->findChild<WhaleViewActions *>();
    QVERIFY(viewActions);

    DragRunner runner([&](QDrag *drag) {
        return dragAndDrop(rightList->viewport(), background(rightList), drag->mimeData(), Qt::ControlModifier);
    });
    dragGesture(leftList, QStringLiteral("a.txt"));
    QTRY_VERIFY(QFile::exists(targetPath(QStringLiteral("a.txt"))) && QFile::exists(targetPath(QStringLiteral("c.txt"))));

    // The dropped items are the selection of the target, as in the file list; the
    // source keeps its selection.
    QTRY_COMPARE(selectedNames(right->view()), QStringList({QStringLiteral("a.txt"), QStringLiteral("c.txt")}));
    QTRY_COMPARE(rightList->selectionModel()->selectedIndexes().count(), 2);
    QCOMPARE(selectedNames(left->view()), QStringList({QStringLiteral("a.txt"), QStringLiteral("c.txt")}));
    QCOMPARE(leftList->selectionModel()->selectedIndexes().count(), 2);

    // The focus is in the target's view, never in a hidden file list.
    QTRY_VERIFY(right->isActive());
    QTRY_COMPARE(m_mainWindow->focusWidget(), rightList);
    QVERIFY(!left->view()->isAncestorOf(m_mainWindow->focusWidget()));
    QVERIFY(!right->view()->isAncestorOf(m_mainWindow->focusWidget()));
    // The gating follows the active half: the test list has no Zoom, but Previews.
    QAction *zoomIn = m_mainWindow->actionCollection()->action(QStringLiteral("view_zoom_in"));
    QVERIFY(!zoomIn->isEnabled());
    QCOMPARE(viewActions->gateReason(QStringLiteral("view_zoom_in")), QStringLiteral("no Zoom capability"));
    QVERIFY(m_mainWindow->actionCollection()->action(QStringLiteral("show_preview"))->isEnabled());
    QVERIFY(m_mainWindow->actionCollection()->action(QStringLiteral("edit_copy"))->isEnabled());
}

QTEST_MAIN(WhaleDragDropTest)

#include "whaledragdroptest.moc"
