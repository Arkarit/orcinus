/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The window of the built-in viewer (V1b, .privatedoc/whale-viewer-plan.md): opening it by
 * activation, paging with keys, wheel and edge clicks, the view following it, load errors.
 */

#include "whaleviewer.h"
#include "dolphinviewcontainer.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistselectionmanager.h"
#include "testdir.h"
#include "views/dolphinview.h"
#include "whaleviewercache.h"
#include "whaleviewersettings.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>

class WhaleViewerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testOpenByActivation();
    void testNotDisplayable();
    void testKeys();
    void testEndOfList();
    void testWrapAround();
    void testWheelAccumulates();
    void testEdgeClicks();
    void testFastPagingLoadsOnlyTheLast();
    void testPreloadsNeighbours();
    void testUpscale();
    void testLoadError();
    void testEscapeCloses();
    void testCloseWhilePaging();
    void testMultiSelectionKeepsSelection();
    void testMultiSelectionMovesSelection();
    void testMixedItems();
    void testManyImagesAskNothing();
    void testOpenAgainReplaces();

private:
    QUrl url(const QString &name) const;
    KFileItem item(const QString &name) const;
    QList<QUrl> selectedUrls() const;
    void select(const QStringList &names, const QString &current);
    WhaleViewer *openByActivation(const QString &name);
    WhaleViewer *openSelection();
    void wheel(QWidget *widget, int delta);

    TestDir *m_testDir = nullptr;
    DolphinViewContainer *m_container = nullptr;
    DolphinView *m_view = nullptr;
    KFileItemModel *m_model = nullptr;
    KItemListController *m_controller = nullptr;
};

static QByteArray imageData()
{
    QImage image(40, 30, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "png");
    return data;
}

void WhaleViewerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    qRegisterMetaType<KItemSet>("KItemSet");
}

void WhaleViewerTest::init()
{
    KSharedConfig::openConfig()->deleteGroup(QStringLiteral("Viewer"));

    m_testDir = new TestDir();
    const QByteArray png = imageData();
    // 1.png … 7.png, broken.png (not an image inside), text.txt
    for (int i = 1; i <= 7; ++i) {
        m_testDir->createFile(QStringLiteral("%1.png").arg(i), png);
    }
    m_testDir->createFile(QStringLiteral("broken.png"), "not an image");
    m_testDir->createFile(QStringLiteral("text.txt"), "text");

    m_container = new DolphinViewContainer(m_testDir->url(), nullptr);
    m_container->resize(600, 500);
    m_container->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_container));
    m_view = m_container->view();
    m_view->setHiddenFilesShown(false);
    m_view->setSortOrder(Qt::AscendingOrder);
    QTRY_COMPARE(m_view->itemsCount(), 9);
    auto itemListContainer = m_view->findChild<KItemListContainer *>();
    QVERIFY(itemListContainer);
    m_controller = itemListContainer->controller();
    m_model = qobject_cast<KFileItemModel *>(m_controller->model());
    QVERIFY(m_model);
}

void WhaleViewerTest::cleanup()
{
    if (WhaleViewer *viewer = WhaleViewer::instance()) {
        viewer->close();
        QTRY_VERIFY(!WhaleViewer::instance());
    }
    delete m_container;
    m_container = nullptr;
    m_view = nullptr;
    m_model = nullptr;
    m_controller = nullptr;
    delete m_testDir;
    m_testDir = nullptr;
}

QUrl WhaleViewerTest::url(const QString &name) const
{
    return QUrl::fromLocalFile(m_testDir->path() + QLatin1Char('/') + name);
}

KFileItem WhaleViewerTest::item(const QString &name) const
{
    return m_model->fileItem(m_model->index(url(name)));
}

QList<QUrl> WhaleViewerTest::selectedUrls() const
{
    QList<QUrl> result;
    const KFileItemList items = m_view->selectedItems();
    for (const KFileItem &item : items) {
        result.append(item.url());
    }
    std::sort(result.begin(), result.end());
    return result;
}

void WhaleViewerTest::select(const QStringList &names, const QString &current)
{
    KItemSet indexes;
    for (const QString &name : names) {
        indexes.insert(m_model->index(url(name)));
    }
    m_controller->selectionManager()->setCurrentItem(m_model->index(url(current)));
    m_controller->selectionManager()->setSelectedItems(indexes);
}

WhaleViewer *WhaleViewerTest::openByActivation(const QString &name)
{
    // As a click: the item becomes the selection, then the view opens it.
    select({name}, name);
    Q_EMIT m_controller->itemActivated(m_model->index(url(name)));
    return WhaleViewer::instance();
}

WhaleViewer *WhaleViewerTest::openSelection()
{
    // As Enter on a multiple selection.
    Q_EMIT m_controller->itemsActivated(m_controller->selectionManager()->selectedItems());
    return WhaleViewer::instance();
}

void WhaleViewerTest::wheel(QWidget *widget, int delta)
{
    const QPointF position(widget->width() / 2.0, widget->height() / 2.0);
    QWheelEvent event(position, widget->mapToGlobal(position), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
}

void WhaleViewerTest::testOpenByActivation()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("3.png"));
    QVERIFY(viewer);
    QVERIFY(viewer->isVisible());
    QVERIFY(viewer->isFullScreen());
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("3.png")));
    // 1–7 and broken.png (an image by its name); not the text file.
    QCOMPARE(viewer->list().count(), 8);
    QVERIFY(!viewer->list().isSelection());
    QTRY_VERIFY(viewer->isLoaded());
    QCOMPARE(viewer->overlayText(), QStringLiteral("3.png"));
    QCOMPARE(m_view->currentItemUrl(), url(QStringLiteral("3.png")));
}

void WhaleViewerTest::testNotDisplayable()
{
    QVERIFY(!WhaleViewer::openItem(item(QStringLiteral("text.txt")), m_view, m_container));
    QVERIFY(!WhaleViewer::instance());
    // Turned off for images.
    WhaleViewerSettings::setMimeTypes({});
    QVERIFY(!WhaleViewer::openItem(item(QStringLiteral("1.png")), m_view, m_container));
    QVERIFY(!WhaleViewer::instance());
}

void WhaleViewerTest::testKeys()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("3.png"));
    QVERIFY(viewer);
    QTest::keyClick(viewer, Qt::Key_Right);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("4.png")));
    QCOMPARE(viewer->overlayText(), QStringLiteral("4.png"));
    // The view follows: current item and selection (V-E8).
    QTRY_COMPARE(m_view->currentItemUrl(), url(QStringLiteral("4.png")));
    QCOMPARE(selectedUrls(), QList<QUrl>{url(QStringLiteral("4.png"))});
    QTRY_VERIFY(viewer->isLoaded());

    QTest::keyClick(viewer, Qt::Key_PageDown);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("5.png")));
    QTest::keyClick(viewer, Qt::Key_Left);
    QTest::keyClick(viewer, Qt::Key_Up);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("3.png")));
    QTest::keyClick(viewer, Qt::Key_End);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("broken.png")));
    QTest::keyClick(viewer, Qt::Key_Home);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("1.png")));
    QTRY_COMPARE(selectedUrls(), QList<QUrl>{url(QStringLiteral("1.png"))});
}

void WhaleViewerTest::testEndOfList()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("1.png"));
    QVERIFY(viewer);
    QTest::keyClick(viewer, Qt::Key_Left);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("1.png")));
    QCOMPARE(viewer->overlayText(), QStringLiteral("First file"));
    QTest::keyClick(viewer, Qt::Key_End);
    QTest::keyClick(viewer, Qt::Key_Right);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("broken.png")));
    QCOMPARE(viewer->overlayText(), QStringLiteral("Last file"));
    // The note goes away again.
    QTRY_VERIFY(viewer->overlayText().isEmpty());
}

void WhaleViewerTest::testWrapAround()
{
    WhaleViewerSettings::setWrapAround(true);
    WhaleViewer *viewer = openByActivation(QStringLiteral("1.png"));
    QVERIFY(viewer);
    QTest::keyClick(viewer, Qt::Key_Left);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("broken.png")));
    QTest::keyClick(viewer, Qt::Key_Right);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("1.png")));
}

void WhaleViewerTest::testWheelAccumulates()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("1.png"));
    QVERIFY(viewer);
    // Down three notches at once.
    wheel(viewer, -360);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("4.png")));
    // A touchpad: half a notch does nothing yet, the second half goes back one.
    wheel(viewer, 60);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("4.png")));
    wheel(viewer, 60);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("3.png")));
}

void WhaleViewerTest::testEdgeClicks()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("3.png"));
    QVERIFY(viewer);
    const int y = viewer->height() / 2;
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, QPoint(viewer->width() / 2, y));
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("3.png")));
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, QPoint(viewer->width() - 5, y));
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("4.png")));
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, QPoint(5, y));
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("3.png")));
    // Other buttons do nothing.
    QTest::mouseClick(viewer, Qt::RightButton, Qt::NoModifier, QPoint(5, y));
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("3.png")));
}

void WhaleViewerTest::testFastPagingLoadsOnlyTheLast()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("1.png"));
    QVERIFY(viewer);
    QTRY_VERIFY(viewer->isLoaded());
    QSignalSpy loaded(viewer, &WhaleViewer::loadFinished);
    for (int i = 0; i < 4; ++i) {
        wheel(viewer, -120);
    }
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("5.png")));
    QTRY_VERIFY(viewer->isLoaded());
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().first().toUrl(), url(QStringLiteral("5.png")));
}

void WhaleViewerTest::testPreloadsNeighbours()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("3.png"));
    QVERIFY(viewer);
    QTRY_VERIFY(viewer->isLoaded());
    // One back, two ahead (V2a).
    QTRY_VERIFY(viewer->cache().entry(url(QStringLiteral("2.png"))));
    QTRY_VERIFY(viewer->cache().entry(url(QStringLiteral("4.png"))));
    QTRY_VERIFY(viewer->cache().entry(url(QStringLiteral("5.png"))));
    QVERIFY(!viewer->cache().entry(url(QStringLiteral("6.png"))));
    QVERIFY(!viewer->cache().isPending(url(QStringLiteral("6.png"))));

    // A preloaded file shows at once.
    QTest::keyClick(viewer, Qt::Key_Right);
    QVERIFY(viewer->isLoaded());
    // Then 6.png is ahead, and 2.png is forgotten.
    QTRY_VERIFY(viewer->cache().entry(url(QStringLiteral("6.png"))));
    QVERIFY(!viewer->cache().entry(url(QStringLiteral("2.png"))));

    // Paging back turns the direction round: two behind.
    QTest::keyClick(viewer, Qt::Key_Left);
    QVERIFY(viewer->isLoaded());
    QTRY_VERIFY(viewer->cache().entry(url(QStringLiteral("2.png"))));
    QTRY_VERIFY(viewer->cache().entry(url(QStringLiteral("1.png"))));
    QVERIFY(viewer->cache().entry(url(QStringLiteral("4.png"))));
    QVERIFY(!viewer->cache().entry(url(QStringLiteral("5.png"))));
}

void WhaleViewerTest::testUpscale()
{
    // The images are 40 × 30: without the setting they stay so (V-E15).
    WhaleViewer *viewer = openByActivation(QStringLiteral("1.png"));
    QVERIFY(viewer);
    QTRY_VERIFY(viewer->isLoaded());
    QCOMPARE(viewer->cache().entry(url(QStringLiteral("1.png")))->scaled.size(), QSize(40, 30));
    viewer->close();
    QTRY_VERIFY(!WhaleViewer::instance());

    WhaleViewerSettings::setUpscale(true);
    viewer = openByActivation(QStringLiteral("1.png"));
    QVERIFY(viewer);
    QTRY_VERIFY(viewer->isLoaded());
    const QSize available = viewer->size() * viewer->devicePixelRatioF();
    QTRY_COMPARE(viewer->cache().entry(url(QStringLiteral("1.png")))->scaled.size(), WhaleViewerCache::fittedSize(QSize(40, 30), available, true));
    QVERIFY(viewer->cache().entry(url(QStringLiteral("1.png")))->scaled.width() > 40);
}

void WhaleViewerTest::testLoadError()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("broken.png"));
    QVERIFY(viewer);
    QTRY_VERIFY(viewer->hasLoadError());
    QVERIFY(!viewer->isLoaded());
    // It stays there (V-E11) and pages on as usual.
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("broken.png")));
    QTest::keyClick(viewer, Qt::Key_Left);
    QVERIFY(!viewer->hasLoadError());
    QTRY_VERIFY(viewer->isLoaded());
}

void WhaleViewerTest::testEscapeCloses()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("2.png"));
    QVERIFY(viewer);
    QTest::keyClick(viewer, Qt::Key_Right);
    QTest::keyClick(viewer, Qt::Key_Escape);
    QTRY_VERIFY(!WhaleViewer::instance());
    // "Find the best photo, Escape, it is selected."
    QCOMPARE(selectedUrls(), QList<QUrl>{url(QStringLiteral("3.png"))});
    QCOMPARE(m_view->currentItemUrl(), url(QStringLiteral("3.png")));
}

void WhaleViewerTest::testCloseWhilePaging()
{
    WhaleViewer *viewer = openByActivation(QStringLiteral("2.png"));
    QVERIFY(viewer);
    // Escape right after the wheel, before the file paged to is loaded.
    wheel(viewer, -240);
    QTest::keyClick(viewer, Qt::Key_Escape);
    QTRY_VERIFY(!WhaleViewer::instance());
    QCOMPARE(selectedUrls(), QList<QUrl>{url(QStringLiteral("4.png"))});
}

void WhaleViewerTest::testMultiSelectionKeepsSelection()
{
    select({QStringLiteral("2.png"), QStringLiteral("text.txt"), QStringLiteral("4.png"), QStringLiteral("6.png")}, QStringLiteral("4.png"));
    WhaleViewer *viewer = openSelection();
    QVERIFY(viewer);
    QVERIFY(viewer->list().isSelection());
    QCOMPARE(viewer->list().count(), 3);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("4.png")));
    QTest::keyClick(viewer, Qt::Key_Right);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("6.png")));
    QTest::keyClick(viewer, Qt::Key_Escape);
    QTRY_VERIFY(!WhaleViewer::instance());
    // A: the selection stays, the current item moved.
    QCOMPARE(m_view->currentItemUrl(), url(QStringLiteral("6.png")));
    QCOMPARE(selectedUrls(),
             (QList<QUrl>{url(QStringLiteral("2.png")), url(QStringLiteral("4.png")), url(QStringLiteral("6.png")), url(QStringLiteral("text.txt"))}));
}

void WhaleViewerTest::testMultiSelectionMovesSelection()
{
    WhaleViewerSettings::setMultiSelection(WhaleViewerSettings::MultiSelection::MoveSelection);
    select({QStringLiteral("2.png"), QStringLiteral("4.png"), QStringLiteral("6.png")}, QStringLiteral("2.png"));
    WhaleViewer *viewer = openSelection();
    QVERIFY(viewer);
    QTest::keyClick(viewer, Qt::Key_End);
    QTest::keyClick(viewer, Qt::Key_Escape);
    QTRY_VERIFY(!WhaleViewer::instance());
    // B: the shown file is the selection.
    QCOMPARE(selectedUrls(), QList<QUrl>{url(QStringLiteral("6.png"))});
}

void WhaleViewerTest::testMixedItems()
{
    const KFileItemList others =
        WhaleViewer::openItems({item(QStringLiteral("1.png")), item(QStringLiteral("text.txt")), item(QStringLiteral("2.png"))}, m_view, m_container);
    QCOMPARE(others.count(), 1);
    QCOMPARE(others.first().url(), url(QStringLiteral("text.txt")));
    QVERIFY(WhaleViewer::instance());
    QCOMPARE(WhaleViewer::instance()->list().count(), 2);
}

void WhaleViewerTest::testManyImagesAskNothing()
{
    // More than five items ask "open %1 items?" first, but images go to one viewer.
    bool asked = false;
    QTimer watchdog;
    connect(&watchdog, &QTimer::timeout, this, [&asked]() {
        if (QWidget *dialog = QApplication::activeModalWidget()) {
            asked = true;
            dialog->close();
        }
    });
    watchdog.start(50);
    select({QStringLiteral("1.png"),
            QStringLiteral("2.png"),
            QStringLiteral("3.png"),
            QStringLiteral("4.png"),
            QStringLiteral("5.png"),
            QStringLiteral("6.png"),
            QStringLiteral("7.png")},
           QStringLiteral("1.png"));
    WhaleViewer *viewer = openSelection();
    QVERIFY(!asked);
    QVERIFY(viewer);
    QCOMPARE(viewer->list().count(), 7);
}

void WhaleViewerTest::testOpenAgainReplaces()
{
    WhaleViewer *first = openByActivation(QStringLiteral("1.png"));
    QVERIFY(first);
    QPointer<WhaleViewer> guard(first);
    QVERIFY(WhaleViewer::openItem(item(QStringLiteral("5.png")), m_view, m_container));
    QVERIFY(WhaleViewer::instance() != first);
    QCOMPARE(WhaleViewer::instance()->currentUrl(), url(QStringLiteral("5.png")));
    QTRY_VERIFY(!guard);
}

QTEST_MAIN(WhaleViewerTest)

#include "whaleviewertest.moc"
