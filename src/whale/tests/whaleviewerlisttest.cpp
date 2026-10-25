/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The built-in viewer (V1a, .privatedoc/whale-viewer-plan.md): which files it shows, the list it
 * pages through, its settings, and how the view follows it.
 */

#include "whaleviewerlist.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistselectionmanager.h"
#include "testdir.h"
#include "views/dolphinview.h"
#include "whaleviewersettings.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QBuffer>
#include <QImage>
#include <QStandardPaths>
#include <QTest>

class WhaleViewerListTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testDisplayable();
    void testDisplayableByMimeType();
    void testVideo();
    void testFolderInViewOrder();
    void testFolderLeavesOutExpandedSubfolders();
    void testFolderOfExpandedSubfolder();
    void testFolderFollowsSortingAndFilter();
    void testFolderOfUnlistedItem();
    void testSelection();
    void testSelectionWithoutDisplayable();
    void testMove();
    void testMoveWrapAround();
    void testSettings();
    void testShowViewerItemSelects();
    void testShowViewerItemKeepsSelection();
    void testShowViewerItemUnlisted();

private:
    QUrl url(const QString &relativePath) const;
    KFileItem item(const QString &relativePath) const;
    QStringList names(const WhaleViewerList &list) const;
    QList<QUrl> selectedUrls() const;

    TestDir *m_testDir = nullptr;
    DolphinView *m_view = nullptr;
    KFileItemModel *m_model = nullptr;
    KItemListSelectionManager *m_selectionManager = nullptr;
    const QStringList m_images{QStringLiteral("image/*")};
};

static QByteArray imageData(const char *format)
{
    QImage image(4, 3, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return data;
}

void WhaleViewerListTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void WhaleViewerListTest::init()
{
    KSharedConfig::openConfig()->deleteGroup(QStringLiteral("Viewer"));

    m_testDir = new TestDir();
    const QByteArray png = imageData("png");
    m_testDir->createFile(QStringLiteral("a.png"), png);
    m_testDir->createFile(QStringLiteral("b.jpg"), imageData("jpg"));
    m_testDir->createFile(QStringLiteral("c.txt"), "text");
    m_testDir->createFile(QStringLiteral("d.png"), png);
    m_testDir->createFile(QStringLiteral(".hidden.png"), png);
    m_testDir->createFile(QStringLiteral("dir/e.png"), png);
    m_testDir->createFile(QStringLiteral("dir/f.png"), png);

    m_view = new DolphinView(m_testDir->url(), nullptr);
    m_view->setViewMode(DolphinView::DetailsView);
    m_view->resize(500, 400);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view));
    // The view properties of an earlier test may still apply.
    m_view->setHiddenFilesShown(false);
    m_view->setSortOrder(Qt::AscendingOrder);
    QTRY_COMPARE(m_view->itemsCount(), 5);
    auto container = m_view->findChild<KItemListContainer *>();
    QVERIFY(container);
    m_model = qobject_cast<KFileItemModel *>(container->controller()->model());
    QVERIFY(m_model);
    m_selectionManager = container->controller()->selectionManager();
}

void WhaleViewerListTest::cleanup()
{
    delete m_view;
    m_view = nullptr;
    m_model = nullptr;
    m_selectionManager = nullptr;
    delete m_testDir;
    m_testDir = nullptr;
}

QUrl WhaleViewerListTest::url(const QString &relativePath) const
{
    return QUrl::fromLocalFile(m_testDir->path() + QLatin1Char('/') + relativePath);
}

KFileItem WhaleViewerListTest::item(const QString &relativePath) const
{
    const int index = m_model->index(url(relativePath));
    return index >= 0 ? m_model->fileItem(index) : KFileItem(url(relativePath));
}

QStringList WhaleViewerListTest::names(const WhaleViewerList &list) const
{
    QStringList result;
    const KFileItemList items = list.items();
    for (const KFileItem &item : items) {
        result.append(item.url().path().mid(m_testDir->path().length() + 1));
    }
    return result;
}

QList<QUrl> WhaleViewerListTest::selectedUrls() const
{
    QList<QUrl> result;
    const KFileItemList items = m_view->selectedItems();
    for (const KFileItem &item : items) {
        result.append(item.url());
    }
    std::sort(result.begin(), result.end());
    return result;
}

void WhaleViewerListTest::testDisplayable()
{
    QVERIFY(WhaleViewerList::isDisplayable(item(QStringLiteral("a.png")), m_images));
    QVERIFY(WhaleViewerList::isDisplayable(item(QStringLiteral("b.jpg")), m_images));
    QVERIFY(!WhaleViewerList::isDisplayable(item(QStringLiteral("c.txt")), m_images));
    QVERIFY(!WhaleViewerList::isDisplayable(item(QStringLiteral("dir")), m_images));
    QVERIFY(!WhaleViewerList::isDisplayable(KFileItem(), m_images));
    // Not local: later stages may fetch it, V1 leaves it to its program.
    QVERIFY(!WhaleViewerList::isDisplayable(KFileItem(QUrl(QStringLiteral("sftp://host/a.png"))), m_images));
    QVERIFY(!WhaleViewerList::isDisplayable(item(QStringLiteral("a.png")), {}));
}

void WhaleViewerListTest::testDisplayableByMimeType()
{
    const QStringList pngOnly{QStringLiteral("image/png")};
    QVERIFY(WhaleViewerList::isDisplayable(item(QStringLiteral("a.png")), pngOnly));
    QVERIFY(!WhaleViewerList::isDisplayable(item(QStringLiteral("b.jpg")), pngOnly));
    // Wanted, but nothing decodes it.
    QVERIFY(!WhaleViewerList::isDisplayable(item(QStringLiteral("c.txt")), {QStringLiteral("text/*")}));
}

void WhaleViewerListTest::testVideo()
{
    m_testDir->createFile(QStringLiteral("clip.mp4"), "not really a video");
    const KFileItem clip(url(QStringLiteral("clip.mp4")));
    QVERIFY(!WhaleViewerList::isVideo(item(QStringLiteral("a.png"))));
    QVERIFY(!WhaleViewerList::isDisplayable(clip, m_images));
    if (!WhaleViewerSettings::videoSupported()) {
        QVERIFY(!WhaleViewerList::isVideo(clip));
        QVERIFY(!WhaleViewerList::isDisplayable(clip, {QStringLiteral("video/*")}));
        QSKIP("built without libmpv");
    }
    // By its type; whether mpv can play it shows when it does (a load error otherwise).
    QVERIFY(WhaleViewerList::isVideo(clip));
    QVERIFY(WhaleViewerList::isDisplayable(clip, {QStringLiteral("video/*")}));
    QVERIFY(WhaleViewerList::isDisplayable(clip, {QStringLiteral("video/mp4")}));
    QVERIFY(!WhaleViewerList::isDisplayable(clip, {QStringLiteral("video/webm")}));
}

void WhaleViewerListTest::testFolderInViewOrder()
{
    const WhaleViewerList list = WhaleViewerList::fromFolder(m_view->items(), item(QStringLiteral("b.jpg")), m_images);
    // No folder, no text file, no hidden file (the view does not list it).
    QCOMPARE(names(list), (QStringList{QStringLiteral("a.png"), QStringLiteral("b.jpg"), QStringLiteral("d.png")}));
    QCOMPARE(list.currentIndex(), 1);
    QCOMPARE(list.currentItem().url(), url(QStringLiteral("b.jpg")));
    QVERIFY(!list.isSelection());

    m_view->setHiddenFilesShown(true);
    QTRY_COMPARE(m_view->itemsCount(), 6);
    const WhaleViewerList withHidden = WhaleViewerList::fromFolder(m_view->items(), item(QStringLiteral("a.png")), m_images);
    QVERIFY(names(withHidden).contains(QStringLiteral(".hidden.png")));
}

void WhaleViewerListTest::testFolderLeavesOutExpandedSubfolders()
{
    QVERIFY(m_model->setExpanded(m_model->index(url(QStringLiteral("dir"))), true));
    QTRY_COMPARE(m_model->count(), 7);
    const WhaleViewerList list = WhaleViewerList::fromFolder(m_view->items(), item(QStringLiteral("a.png")), m_images);
    QCOMPARE(names(list), (QStringList{QStringLiteral("a.png"), QStringLiteral("b.jpg"), QStringLiteral("d.png")}));
}

void WhaleViewerListTest::testFolderOfExpandedSubfolder()
{
    QVERIFY(m_model->setExpanded(m_model->index(url(QStringLiteral("dir"))), true));
    QTRY_COMPARE(m_model->count(), 7);
    const WhaleViewerList list = WhaleViewerList::fromFolder(m_view->items(), item(QStringLiteral("dir/f.png")), m_images);
    QCOMPARE(names(list), (QStringList{QStringLiteral("dir/e.png"), QStringLiteral("dir/f.png")}));
    QCOMPARE(list.currentIndex(), 1);
}

void WhaleViewerListTest::testFolderFollowsSortingAndFilter()
{
    m_view->setSortOrder(Qt::DescendingOrder);
    QTRY_COMPARE(m_view->items().first().url(), url(QStringLiteral("dir")));
    QTRY_COMPARE(m_view->items().at(1).url(), url(QStringLiteral("d.png")));
    const WhaleViewerList sorted = WhaleViewerList::fromFolder(m_view->items(), item(QStringLiteral("a.png")), m_images);
    QCOMPARE(names(sorted), (QStringList{QStringLiteral("d.png"), QStringLiteral("b.jpg"), QStringLiteral("a.png")}));
    QCOMPARE(sorted.currentIndex(), 2);

    m_view->setNameFilter(QStringLiteral("png"));
    QTRY_COMPARE(m_view->itemsCount(), 2);
    const WhaleViewerList filtered = WhaleViewerList::fromFolder(m_view->items(), item(QStringLiteral("a.png")), m_images);
    QCOMPARE(names(filtered), (QStringList{QStringLiteral("d.png"), QStringLiteral("a.png")}));
}

void WhaleViewerListTest::testFolderOfUnlistedItem()
{
    // E.g. opened from a plugin view deep below the location: just that file.
    const WhaleViewerList list = WhaleViewerList::fromFolder(m_view->items(), KFileItem(url(QStringLiteral("dir/e.png"))), m_images);
    QCOMPARE(names(list), QStringList{QStringLiteral("dir/e.png")});
    QCOMPARE(list.currentIndex(), 0);
}

void WhaleViewerListTest::testSelection()
{
    const KFileItemList selected{item(QStringLiteral("a.png")), item(QStringLiteral("c.txt")), item(QStringLiteral("d.png"))};
    const WhaleViewerList list = WhaleViewerList::fromSelection(selected, url(QStringLiteral("d.png")), m_images);
    QCOMPARE(names(list), (QStringList{QStringLiteral("a.png"), QStringLiteral("d.png")}));
    QCOMPARE(list.currentIndex(), 1);
    QVERIFY(list.isSelection());

    // The current item is not displayable: from the first one.
    const WhaleViewerList fromFirst = WhaleViewerList::fromSelection(selected, url(QStringLiteral("c.txt")), m_images);
    QCOMPARE(fromFirst.currentIndex(), 0);
}

void WhaleViewerListTest::testSelectionWithoutDisplayable()
{
    const WhaleViewerList list = WhaleViewerList::fromSelection({item(QStringLiteral("c.txt")), item(QStringLiteral("dir"))}, QUrl(), m_images);
    QVERIFY(list.isEmpty());
    QCOMPARE(list.currentIndex(), -1);
    QVERIFY(list.currentItem().isNull());
    QVERIFY(!WhaleViewerList().move(1, false));
}

void WhaleViewerListTest::testMove()
{
    WhaleViewerList list = WhaleViewerList::fromFolder(m_view->items(), item(QStringLiteral("a.png")), m_images);
    QVERIFY(!list.move(1, false));
    QCOMPARE(list.currentIndex(), 1);
    // Three steps on from b.jpg: stops at the last file.
    QVERIFY(list.move(3, false));
    QCOMPARE(list.currentIndex(), 2);
    QVERIFY(list.move(1, false));
    QCOMPARE(list.currentIndex(), 2);
    QVERIFY(!list.move(-2, false));
    QCOMPARE(list.currentIndex(), 0);
    QVERIFY(list.move(-1, false));
    QCOMPARE(list.currentIndex(), 0);

    list.setCurrentIndex(2);
    QCOMPARE(list.currentIndex(), 2);
    list.setCurrentIndex(3);
    QCOMPARE(list.currentIndex(), 2);
}

void WhaleViewerListTest::testMoveWrapAround()
{
    WhaleViewerList list = WhaleViewerList::fromFolder(m_view->items(), item(QStringLiteral("d.png")), m_images);
    QVERIFY(!list.move(1, true));
    QCOMPARE(list.currentIndex(), 0);
    QVERIFY(!list.move(-1, true));
    QCOMPARE(list.currentIndex(), 2);
    QVERIFY(!list.move(-7, true));
    QCOMPARE(list.currentIndex(), 1);
}

void WhaleViewerListTest::testSettings()
{
    QStringList defaults{QStringLiteral("image/*")};
    if (WhaleViewerSettings::videoSupported()) {
        defaults.append(QStringLiteral("video/*"));
    }
    QCOMPARE(WhaleViewerSettings::mimeTypes(), defaults);
    QVERIFY(WhaleViewerSettings::videoAutoplay());
    QVERIFY(WhaleViewerSettings::videoSound());
    QVERIFY(WhaleViewerSettings::videoLoop());
    QVERIFY(!WhaleViewerSettings::wrapAround());
    QCOMPARE(WhaleViewerSettings::multiSelection(), WhaleViewerSettings::MultiSelection::KeepSelection);

    WhaleViewerSettings::setMimeTypes({QStringLiteral("image/png"), QStringLiteral("video/*")});
    WhaleViewerSettings::setWrapAround(true);
    WhaleViewerSettings::setMultiSelection(WhaleViewerSettings::MultiSelection::MoveSelection);
    QCOMPARE(WhaleViewerSettings::mimeTypes(), (QStringList{QStringLiteral("image/png"), QStringLiteral("video/*")}));
    QVERIFY(WhaleViewerSettings::wrapAround());
    QCOMPARE(WhaleViewerSettings::multiSelection(), WhaleViewerSettings::MultiSelection::MoveSelection);

    const KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("Viewer"));
    QCOMPARE(group.readEntry("MultiSelection", QString()), QStringLiteral("MoveSelection"));
    WhaleViewerSettings::setMultiSelection(WhaleViewerSettings::MultiSelection::KeepSelection);
    QVERIFY(!group.hasKey("MultiSelection"));

    // An empty list is a choice of its own: the viewer opens nothing.
    WhaleViewerSettings::setMimeTypes({});
    QVERIFY(WhaleViewerSettings::mimeTypes().isEmpty());
}

void WhaleViewerListTest::testShowViewerItemSelects()
{
    m_view->markUrlsAsSelected({url(QStringLiteral("a.png"))});
    QVERIFY(m_view->showViewerItem(url(QStringLiteral("d.png")), true));
    QCOMPARE(m_view->currentItemUrl(), url(QStringLiteral("d.png")));
    QCOMPARE(selectedUrls(), QList<QUrl>{url(QStringLiteral("d.png"))});
}

void WhaleViewerListTest::testShowViewerItemKeepsSelection()
{
    KItemSet selected;
    selected.insert(m_model->index(url(QStringLiteral("a.png"))));
    selected.insert(m_model->index(url(QStringLiteral("d.png"))));
    m_selectionManager->setSelectedItems(selected);
    QVERIFY(m_view->showViewerItem(url(QStringLiteral("d.png")), false));
    QCOMPARE(m_view->currentItemUrl(), url(QStringLiteral("d.png")));
    QVERIFY(m_view->showViewerItem(url(QStringLiteral("a.png")), false));
    QCOMPARE(m_view->currentItemUrl(), url(QStringLiteral("a.png")));
    QCOMPARE(selectedUrls(), (QList<QUrl>{url(QStringLiteral("a.png")), url(QStringLiteral("d.png"))}));
}

void WhaleViewerListTest::testShowViewerItemUnlisted()
{
    m_selectionManager->setCurrentItem(m_model->index(url(QStringLiteral("a.png"))));
    QVERIFY(!m_view->showViewerItem(url(QStringLiteral("dir/e.png")), true));
    QCOMPARE(m_view->currentItemUrl(), url(QStringLiteral("a.png")));
    QVERIFY(m_view->selectedItems().isEmpty());
}

QTEST_MAIN(WhaleViewerListTest)

#include "whaleviewerlisttest.moc"
