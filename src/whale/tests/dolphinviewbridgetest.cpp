/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinviewbridge.h"
#include "testdir.h"
#include "views/dolphinview.h"
#include "whaleitemlist.h"
#include "whaleview.h"
#include "whaleviewcontextimpl.h"
#include "whaleviewhost.h"
#include "whaleviewregistry.h"

#include <KIO/CopyJob>
#include <KIO/DeleteJob>

#include <QListView>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

namespace
{
const QString testListId = QStringLiteral("whale_testlist");

QList<QUrl> urls(const KFileItemList &items)
{
    QList<QUrl> result;
    for (const KFileItem &item : items) {
        result.append(item.url());
    }
    return result;
}

QStringList names(const KFileItemList &items)
{
    QStringList result;
    for (const KFileItem &item : items) {
        result.append(item.text());
    }
    return result;
}

/// Deletes through KIO, which tells the DolphinView at once. (KDirWatch does not
/// reliably report deletions in tests; Dolphin's own tests update the lister instead.)
bool deleteFile(const TestDir &dir, const QString &name)
{
    return KIO::del(QUrl::fromLocalFile(dir.path() + QLatin1Char('/') + name), KIO::HideProgressInfo)->exec();
}

QStringList rows(const QAbstractItemView *view)
{
    QStringList result;
    const QAbstractItemModel *model = view->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        result.append(model->index(row, 0).data().toString());
    }
    return result;
}

/// Applies the signals of a WhaleItemList to its own copy, as a view would.
class ListFollower : public QObject
{
public:
    explicit ListFollower(WhaleItemList *list)
        : m_items(list->items())
    {
        connect(list, &WhaleItemList::itemsReset, this, [this, list]() {
            ++resets;
            m_items = list->items();
        });
        connect(list, &WhaleItemList::itemsInserted, this, [this](int index, const KFileItemList &items) {
            ++inserts;
            for (int i = 0; i < items.count(); ++i) {
                m_items.insert(index + i, items.at(i));
            }
        });
        connect(list, &WhaleItemList::itemsRemoved, this, [this](int index, const KFileItemList &items) {
            ++removes;
            QCOMPARE(urls(m_items.mid(index, items.count())), urls(items));
            m_items.remove(index, items.count());
        });
        connect(list, &WhaleItemList::itemsMoved, this, [this](int index, const QList<int> &movedToIndexes) {
            ++moves;
            const KFileItemList old = m_items.mid(index, movedToIndexes.count());
            for (int i = 0; i < old.count(); ++i) {
                m_items[movedToIndexes.at(i)] = old.at(i);
            }
        });
        connect(list, &WhaleItemList::itemsChanged, this, [this](int index, const KFileItemList &items) {
            ++changes;
            for (int i = 0; i < items.count(); ++i) {
                m_items[index + i] = items.at(i);
            }
        });
    }

    KFileItemList items() const
    {
        return m_items;
    }

    int resets = 0;
    int inserts = 0;
    int removes = 0;
    int moves = 0;
    int changes = 0;

private:
    KFileItemList m_items;
};
}

class DolphinViewBridgeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testInitialItems();
    void testLoading();
    void testCreate();
    void testRemove();
    void testRename();
    void testChange();
    void testSorting();
    void testHiddenFiles();
    void testNameFilter();
    void testNavigation();
    void testExpandedFolders();
    void testItemListLifetime();
    void testTestListView();
    void testTestListViewIsReadOnly();

private:
    /// The list must have followed the DolphinView in small steps, never by a reset.
    bool mirrors(const WhaleItemList *list, const ListFollower &follower) const;
    QListView *showTestListView();

    QTemporaryDir m_tmpHome;
    QScopedPointer<TestDir> m_dir;
    QScopedPointer<QWidget> m_window;
    DolphinView *m_view = nullptr;
    WhaleViewHost *m_host = nullptr;
    QScopedPointer<DolphinViewBridge> m_bridge;
    WhaleViewRegistry m_registry;
};

void DolphinViewBridgeTest::initTestCase()
{
    QVERIFY(m_tmpHome.isValid());
    qputenv("HOME", m_tmpHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::addLibraryPath(QStringLiteral(WHALE_TEST_PLUGIN_DIR));
}

void DolphinViewBridgeTest::init()
{
    // A list that had to be reloaded completely missed a step of the model.
    QTest::failOnWarning(QRegularExpression(QStringLiteral("out of step")));

    m_dir.reset(new TestDir());
    m_dir->createFile(QStringLiteral("a.txt"), QByteArray(300, 'a'));
    m_dir->createFile(QStringLiteral("b.txt"), QByteArray(100, 'b'));
    m_dir->createFile(QStringLiteral("c.txt"), QByteArray(200, 'c'));
    m_dir->createFile(QStringLiteral(".hidden"));
    m_dir->createFile(QStringLiteral("sub/inner.txt"));
    m_dir->createDir(QStringLiteral("sub/deeper"));

    m_window.reset(new QWidget);
    m_view = new DolphinView(m_dir->url(), nullptr);
    m_host = new WhaleViewHost(m_view, m_window.data());
    auto layout = new QVBoxLayout(m_window.data());
    layout->addWidget(m_host);
    m_bridge.reset(new DolphinViewBridge(m_view));
    // Whale shares the view properties of all folders by default, so the changes
    // of an earlier test would still apply.
    m_view->setViewMode(DolphinView::IconsView);
    m_view->setHiddenFilesShown(false);
    m_view->setSortRole("text");
    m_view->setSortOrder(Qt::AscendingOrder);
    m_view->setSortFoldersFirst(true);
    m_window->resize(600, 400);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.data()));
    QTRY_COMPARE(m_view->itemsCount(), 4);
}

void DolphinViewBridgeTest::cleanup()
{
    m_bridge.reset();
    m_window.reset();
    m_view = nullptr;
    m_host = nullptr;
    m_dir.reset();
}

bool DolphinViewBridgeTest::mirrors(const WhaleItemList *list, const ListFollower &follower) const
{
    const QList<QUrl> expected = urls(m_view->items());
    return urls(list->items()) == expected && urls(follower.items()) == expected && follower.resets == 0;
}

QListView *DolphinViewBridgeTest::showTestListView()
{
    WhaleViewPlugin *plugin = m_registry.plugin(testListId);
    if (!plugin || !m_host->showPluginView(testListId, plugin)) {
        return nullptr;
    }
    return qobject_cast<QListView *>(m_host->currentView()->widget());
}

void DolphinViewBridgeTest::testInitialItems()
{
    // Created after loading: a copy of the loaded items, in the DolphinView's order.
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    QCOMPARE(list->parent(), &owner);
    QCOMPARE(list->count(), 4);
    QCOMPARE(urls(list->items()), urls(m_view->items()));
    QCOMPARE(names(list->items()), QStringList({QStringLiteral("sub"), QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}));
    QCOMPARE(list->item(0).url(), m_view->items().at(0).url());
    QVERIFY(list->item(4).isNull());
    QVERIFY(list->item(-1).isNull());

    // Created before loading: follows the loading in steps.
    TestDir other;
    other.createFiles({QStringLiteral("x.txt"), QStringLiteral("y.txt")});
    QObject owner2;
    WhaleItemList *list2 = m_bridge->createItemList(&owner2);
    ListFollower follower(list2);
    QSignalSpy completed(m_view, &DolphinView::directoryLoadingCompleted);
    m_view->setUrl(other.url());
    QVERIFY(completed.wait(5000));
    QCOMPARE(names(list2->items()), QStringList({QStringLiteral("x.txt"), QStringLiteral("y.txt")}));
    QVERIFY(mirrors(list2, follower));
    QVERIFY(follower.removes > 0);
    QVERIFY(follower.inserts > 0);
}

void DolphinViewBridgeTest::testLoading()
{
    WhaleViewContextImpl context(m_bridge.data());
    QCOMPARE(context.url(), m_view->url());
    QVERIFY(context.items());
    QCOMPARE(context.items(), context.items()); // one list per context
    QCOMPARE(context.items()->parent(), &context);

    QSignalSpy urlChanged(&context, &WhaleViewContext::urlChanged);
    QSignalSpy started(&context, &WhaleViewContext::loadingStarted);
    QSignalSpy completed(&context, &WhaleViewContext::loadingCompleted);
    const QUrl subUrl = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub"));
    m_view->setUrl(subUrl);
    QCOMPARE(urlChanged.count(), 1);
    QCOMPARE(urlChanged.at(0).at(0).toUrl(), subUrl);
    QCOMPARE(context.url(), subUrl);
    QVERIFY(completed.wait(5000));
    QVERIFY(started.count() >= 1);
    // When loadingCompleted() arrives, the items are there.
    QCOMPARE(names(context.items()->items()), QStringList({QStringLiteral("deeper"), QStringLiteral("inner.txt")}));

    // Canceling, e.g. when stopping the loading of a location.
    QSignalSpy canceled(&context, &WhaleViewContext::loadingCanceled);
    m_view->setUrl(m_dir->url());
    m_view->stopLoading();
    QCOMPARE(canceled.count(), 1);
}

void DolphinViewBridgeTest::testCreate()
{
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);

    m_dir->createFile(QStringLiteral("new.txt"));
    m_dir->createDir(QStringLiteral("newdir"));
    QTRY_COMPARE(list->count(), 6);
    QTRY_VERIFY(mirrors(list, follower));
    QVERIFY(names(list->items()).contains(QStringLiteral("new.txt")));
    QVERIFY(names(list->items()).contains(QStringLiteral("newdir")));
    QVERIFY(follower.inserts > 0);
}

void DolphinViewBridgeTest::testRemove()
{
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);

    QVERIFY(deleteFile(*m_dir, QStringLiteral("b.txt")));
    QTRY_COMPARE(list->count(), 3);
    QVERIFY(mirrors(list, follower));
    QVERIFY(!names(list->items()).contains(QStringLiteral("b.txt")));
    QCOMPARE(follower.removes, 1);
}

void DolphinViewBridgeTest::testRename()
{
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);

    // A rename through KIO, which Dolphin sees as a change of the item.
    const QUrl from = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/a.txt"));
    const QUrl to = QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/z.txt"));
    KIO::CopyJob *job = KIO::moveAs(from, to, KIO::HideProgressInfo);
    QVERIFY(job->exec());
    // The model replaces the item in place and sorts it in a moment later.
    QTRY_COMPARE(names(list->items()), QStringList({QStringLiteral("sub"), QStringLiteral("b.txt"), QStringLiteral("c.txt"), QStringLiteral("z.txt")}));
    QTRY_VERIFY(mirrors(list, follower));

    // A rename outside of KIO.
    QVERIFY(QFile::rename(m_dir->path() + QStringLiteral("/b.txt"), m_dir->path() + QStringLiteral("/0.txt")));
    QTRY_COMPARE(names(list->items()), QStringList({QStringLiteral("sub"), QStringLiteral("0.txt"), QStringLiteral("c.txt"), QStringLiteral("z.txt")}));
    QTRY_VERIFY(mirrors(list, follower));
}

void DolphinViewBridgeTest::testChange()
{
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);
    QSignalSpy changed(list, &WhaleItemList::itemsChanged);

    QFile file(m_dir->path() + QStringLiteral("/b.txt"));
    QVERIFY(file.open(QIODevice::Append));
    file.write(QByteArray(1000, 'x'));
    file.close();

    auto sizeOfB = [list]() {
        const KFileItemList items = list->items();
        for (const KFileItem &item : items) {
            if (item.text() == QLatin1String("b.txt")) {
                return item.size();
            }
        }
        return KIO::filesize_t(0);
    };
    QTRY_COMPARE(sizeOfB(), KIO::filesize_t(1100));
    QVERIFY(changed.count() >= 1);
    QCOMPARE(changed.last().at(1).value<KFileItemList>().first().text(), QStringLiteral("b.txt"));
    QTRY_VERIFY(mirrors(list, follower));
}

void DolphinViewBridgeTest::testSorting()
{
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);

    m_view->setSortOrder(Qt::DescendingOrder);
    QTRY_COMPARE(names(list->items()), QStringList({QStringLiteral("sub"), QStringLiteral("c.txt"), QStringLiteral("b.txt"), QStringLiteral("a.txt")}));
    QVERIFY(mirrors(list, follower));

    // setSortRole() also switches to the preferred order of the role.
    m_view->setSortFoldersFirst(false);
    m_view->setSortRole("size");
    m_view->setSortOrder(Qt::AscendingOrder);
    auto files = [list]() {
        QStringList result = names(list->items());
        result.removeAll(QStringLiteral("sub"));
        return result;
    };
    QTRY_COMPARE(files(), QStringList({QStringLiteral("b.txt"), QStringLiteral("c.txt"), QStringLiteral("a.txt")}));
    QTRY_VERIFY(mirrors(list, follower));
    QVERIFY(follower.moves > 0);
}

void DolphinViewBridgeTest::testHiddenFiles()
{
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);

    m_view->setHiddenFilesShown(true);
    QTRY_VERIFY(names(list->items()).contains(QStringLiteral(".hidden")));
    QVERIFY(mirrors(list, follower));

    m_view->setHiddenFilesShown(false);
    QTRY_VERIFY(!names(list->items()).contains(QStringLiteral(".hidden")));
    QCOMPARE(list->count(), 4);
    QVERIFY(mirrors(list, follower));
}

void DolphinViewBridgeTest::testNameFilter()
{
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);

    m_view->setNameFilter(QStringLiteral("b."));
    QTRY_COMPARE(names(list->items()), QStringList({QStringLiteral("b.txt")}));
    QVERIFY(mirrors(list, follower));

    m_view->setNameFilter(QString());
    QTRY_COMPARE(list->count(), 4);
    QVERIFY(mirrors(list, follower));
}

void DolphinViewBridgeTest::testNavigation()
{
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);
    QSignalSpy completed(m_view, &DolphinView::directoryLoadingCompleted);

    m_view->setUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QVERIFY(completed.wait(5000));
    QCOMPARE(names(list->items()), QStringList({QStringLiteral("deeper"), QStringLiteral("inner.txt")}));
    QVERIFY(mirrors(list, follower));

    m_view->setUrl(m_dir->url());
    QVERIFY(completed.wait(5000));
    QCOMPARE(list->count(), 4);
    QVERIFY(mirrors(list, follower));
}

void DolphinViewBridgeTest::testExpandedFolders()
{
    // The details view can show the content of folders below them. The list holds
    // exactly what the DolphinView shows, so these items appear in it as well.
    m_view->setViewMode(DolphinView::DetailsView);
    QObject owner;
    WhaleItemList *list = m_bridge->createItemList(&owner);
    ListFollower follower(list);

    m_view->expandToUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub/deeper")));
    QTRY_COMPARE(list->count(), 6);
    QCOMPARE(names(list->items()).mid(0, 3), QStringList({QStringLiteral("sub"), QStringLiteral("deeper"), QStringLiteral("inner.txt")}));
    QVERIFY(mirrors(list, follower));
}

void DolphinViewBridgeTest::testItemListLifetime()
{
    // A destroyed list (with its context or plugin view) stops following the model.
    auto owner = std::make_unique<QObject>();
    QPointer<WhaleItemList> list = m_bridge->createItemList(owner.get());
    owner.reset();
    QVERIFY(!list);
    m_dir->createFile(QStringLiteral("new.txt"));
    QTRY_COMPARE(m_view->itemsCount(), 5);

    // The plugin view's context and list go with the view, and they come back fresh.
    QListView *listView = showTestListView();
    QVERIFY(listView);
    QPointer<QAbstractItemModel> model = listView->model();
    m_host->showDolphinView();
    QVERIFY(!model);
    QVERIFY(deleteFile(*m_dir, QStringLiteral("new.txt")));
    QTRY_COMPARE(m_view->itemsCount(), 4);
    listView = showTestListView();
    QVERIFY(listView);
    QCOMPARE(rows(listView), names(m_view->items()));
}

void DolphinViewBridgeTest::testTestListView()
{
    QListView *listView = showTestListView();
    QVERIFY(listView);
    QVERIFY(listView->isVisible());
    QVERIFY(!m_view->isVisible());
    QCOMPARE(m_host->currentView()->capabilities(), WhaleViewCapabilities(WhaleViewCapability::HostOrder));
    QCOMPARE(rows(listView), names(m_view->items()));

    // Host order: sorting, hidden files and the name filter of the DolphinView apply.
    m_view->setSortOrder(Qt::DescendingOrder);
    QTRY_COMPARE(rows(listView), QStringList({QStringLiteral("sub"), QStringLiteral("c.txt"), QStringLiteral("b.txt"), QStringLiteral("a.txt")}));
    m_view->setSortOrder(Qt::AscendingOrder);
    QTRY_COMPARE(rows(listView), QStringList({QStringLiteral("sub"), QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}));

    m_view->setHiddenFilesShown(true);
    QTRY_VERIFY(rows(listView).contains(QStringLiteral(".hidden")));
    QCOMPARE(rows(listView), names(m_view->items()));
    m_view->setHiddenFilesShown(false);
    QTRY_VERIFY(!rows(listView).contains(QStringLiteral(".hidden")));

    m_view->setNameFilter(QStringLiteral("c."));
    QTRY_COMPARE(rows(listView), QStringList({QStringLiteral("c.txt")}));
    m_view->setNameFilter(QString());
    QTRY_COMPARE(rows(listView).count(), 4);

    // File system changes.
    m_dir->createFile(QStringLiteral("new.txt"));
    QTRY_VERIFY(rows(listView).contains(QStringLiteral("new.txt")));
    QVERIFY(deleteFile(*m_dir, QStringLiteral("a.txt")));
    QTRY_VERIFY(!rows(listView).contains(QStringLiteral("a.txt")));
    QCOMPARE(rows(listView), names(m_view->items()));

    // Navigation.
    QSignalSpy completed(m_view, &DolphinView::directoryLoadingCompleted);
    m_view->setUrl(QUrl::fromLocalFile(m_dir->path() + QStringLiteral("/sub")));
    QVERIFY(completed.wait(5000));
    QCOMPARE(rows(listView), QStringList({QStringLiteral("deeper"), QStringLiteral("inner.txt")}));

    // The current item stays on its file when the order changes.
    listView->setCurrentIndex(listView->model()->index(1, 0));
    QCOMPARE(listView->currentIndex().data().toString(), QStringLiteral("inner.txt"));
    m_view->setSortFoldersFirst(false);
    m_view->setSortOrder(Qt::DescendingOrder);
    QTRY_COMPARE(rows(listView), QStringList({QStringLiteral("inner.txt"), QStringLiteral("deeper")}));
    QCOMPARE(listView->currentIndex().data().toString(), QStringLiteral("inner.txt"));
}

void DolphinViewBridgeTest::testTestListViewIsReadOnly()
{
    QListView *listView = showTestListView();
    QVERIFY(listView);
    QTRY_COMPARE(rows(listView).count(), 4);

    // Nothing in the test view may look like a target of the host's file actions.
    QCOMPARE(listView->selectionMode(), QAbstractItemView::NoSelection);
    QCOMPARE(listView->editTriggers(), QAbstractItemView::NoEditTriggers);
    QVERIFY(!listView->dragEnabled());
    QVERIFY(!listView->acceptDrops());
    QCOMPARE(listView->contextMenuPolicy(), Qt::PreventContextMenu);

    // Clicking and double clicking an item neither selects nor opens it, and leaves
    // the selection of the hidden DolphinView alone.
    const QRect rect = listView->visualRect(listView->model()->index(1, 0));
    QTest::mouseClick(listView->viewport(), Qt::LeftButton, {}, rect.center());
    QTest::mouseDClick(listView->viewport(), Qt::LeftButton, {}, rect.center());
    QVERIFY(listView->selectionModel()->selectedIndexes().isEmpty());
    QCOMPARE(m_view->selectedItemsCount(), 0);
    QCOMPARE(m_view->url(), m_dir->url());
    QCOMPARE(listView->model()->rowCount(), 4);
}

QTEST_MAIN(DolphinViewBridgeTest)

#include "dolphinviewbridgetest.moc"
