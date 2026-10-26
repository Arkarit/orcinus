/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The window of the built-in viewer (V1b, .privatedoc/whale-viewer-plan.md): opening it by
 * activation, paging with keys, wheel and edge clicks, the view following it, load errors.
 */

#include "whaleviewer.h"
#include "config-dolphin.h"
#include "dolphinviewcontainer.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistselectionmanager.h"
#include "testdir.h"
#include "views/dolphinview.h"
#include "whaletestvideo.h"
#include "whaleviewercache.h"
#include "whaleviewersettings.h"
#if HAVE_MPV
#include "whalevideoplayer.h"
#endif

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
    void testZoomTowardsThePointer();
    void testZoomKeys();
    void testPan();
    void testPagingFitsAgain();
    void testZoomSharpOrSmooth();
    void testAnimation();
    void testVideo();
    void testVideoError();
    void testVideoSettings();
    void testVideoControls();
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
    void ctrlWheel(QWidget *widget, const QPointF &position, int delta);
    void mouse(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPointF &position);
    WhaleViewer *openImage(const QString &name, const QImage &image);
    WhaleViewer *openFile(const QString &name, const QByteArray &data);

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

void WhaleViewerTest::ctrlWheel(QWidget *widget, const QPointF &position, int delta)
{
    QWheelEvent event(position, widget->mapToGlobal(position), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
}

void WhaleViewerTest::mouse(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPointF &position)
{
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(Qt::MiddleButton);
    QMouseEvent event(type, position, widget->mapToGlobal(position), type == QEvent::MouseMove ? Qt::NoButton : button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

WhaleViewer *WhaleViewerTest::openImage(const QString &name, const QImage &image)
{
    // Another image than the small ones of init(), added to the folder.
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "png");
    return openFile(name, data);
}

WhaleViewer *WhaleViewerTest::openFile(const QString &name, const QByteArray &data)
{
    m_testDir->createFile(name, data);
    const int count = m_view->itemsCount();
    if (!QTest::qWaitFor([this, count]() {
            return m_view->itemsCount() > count;
        })) {
        return nullptr;
    }
    WhaleViewer *viewer = openByActivation(name);
    if (!viewer || !QTest::qWaitFor([viewer]() {
            return viewer->isLoaded();
        })) {
        return nullptr;
    }
    return viewer;
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

static QImage largeImage()
{
    QImage image(4000, 3000, QImage::Format_RGB32);
    image.fill(Qt::darkGreen);
    return image;
}

static bool fuzzyEqual(const QPointF &a, const QPointF &b)
{
    return qAbs(a.x() - b.x()) < 1 && qAbs(a.y() - b.y()) < 1;
}

void WhaleViewerTest::testZoomTowardsThePointer()
{
    WhaleViewer *viewer = openImage(QStringLiteral("large.png"), largeImage());
    QVERIFY(viewer);
    QVERIFY(!viewer->isZoomed());
    const qreal fitted = viewer->zoom();
    QVERIFY(fitted > 0 && fitted < 1);
    QVERIFY(fuzzyEqual(viewer->visibleImageRect().topLeft(), QPointF(0, 0)));
    QVERIFY(fuzzyEqual(viewer->visibleImageRect().bottomRight(), QPointF(4000, 3000)));

    const QPointF pointer(viewer->width() / 4.0, viewer->height() / 3.0);
    ctrlWheel(viewer, pointer, 120);
    QVERIFY(viewer->isZoomed());
    QVERIFY(qFuzzyCompare(viewer->zoom(), fitted * 1.25));
    // Not paged.
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("large.png")));

    // Ctrl+wheel: the point of the image under the pointer stays under it (V-E5), once the image
    // is larger than the window (smaller, it stays centred).
    ctrlWheel(viewer, pointer, 360);
    const QPointF before = viewer->imagePosition(pointer);
    ctrlWheel(viewer, pointer, 120);
    QVERIFY(fuzzyEqual(viewer->imagePosition(pointer), before));
    ctrlWheel(viewer, pointer, -120);
    QVERIFY(fuzzyEqual(viewer->imagePosition(pointer), before));
    // Back down to the fitted size: fitted again.
    ctrlWheel(viewer, pointer, -480);
    QVERIFY(!viewer->isZoomed());
    QVERIFY(qFuzzyCompare(viewer->zoom(), fitted));
    ctrlWheel(viewer, pointer, -120);
    QVERIFY(!viewer->isZoomed());
}

void WhaleViewerTest::testZoomKeys()
{
    WhaleViewer *viewer = openImage(QStringLiteral("large.png"), largeImage());
    QVERIFY(viewer);
    const qreal fitted = viewer->zoom();
    // 1: 100 %, one image pixel per device pixel (V-E12).
    QTest::keyClick(viewer, Qt::Key_1);
    QVERIFY(viewer->isZoomed());
    QCOMPARE(viewer->zoom(), 1.0);
    const qreal dpr = viewer->devicePixelRatioF();
    QVERIFY(qAbs(viewer->visibleImageRect().width() - viewer->width() * dpr) < 1);
    // Towards the middle: it stays in the middle.
    QVERIFY(fuzzyEqual(viewer->visibleImageRect().center(), QPointF(2000, 1500)));
    QTest::keyClick(viewer, Qt::Key_Plus);
    QCOMPARE(viewer->zoom(), 1.25);
    QTest::keyClick(viewer, Qt::Key_Minus);
    QCOMPARE(viewer->zoom(), 1.0);
    QTest::keyClick(viewer, Qt::Key_0);
    QVERIFY(!viewer->isZoomed());
    QVERIFY(qFuzzyCompare(viewer->zoom(), fitted));
    // Up to 3200 %.
    for (int i = 0; i < 40; ++i) {
        QTest::keyClick(viewer, Qt::Key_Plus);
    }
    QCOMPARE(viewer->zoom(), 32.0);
}

void WhaleViewerTest::testPan()
{
    WhaleViewer *viewer = openImage(QStringLiteral("large.png"), largeImage());
    QVERIFY(viewer);
    // Not zoomed: the middle button does nothing.
    const QPointF middle(viewer->width() / 2.0, viewer->height() / 2.0);
    const QRectF fitted = viewer->visibleImageRect();
    mouse(viewer, QEvent::MouseButtonPress, Qt::MiddleButton, middle);
    mouse(viewer, QEvent::MouseMove, Qt::MiddleButton, middle + QPointF(50, 50));
    mouse(viewer, QEvent::MouseButtonRelease, Qt::MiddleButton, middle + QPointF(50, 50));
    QCOMPARE(viewer->visibleImageRect(), fitted);

    QTest::keyClick(viewer, Qt::Key_1);
    const QRectF before = viewer->visibleImageRect();
    // Dragging the image right and down shows more of its left and top.
    mouse(viewer, QEvent::MouseButtonPress, Qt::MiddleButton, middle);
    mouse(viewer, QEvent::MouseMove, Qt::MiddleButton, middle + QPointF(100, 40));
    mouse(viewer, QEvent::MouseButtonRelease, Qt::MiddleButton, middle + QPointF(100, 40));
    const qreal dpr = viewer->devicePixelRatioF();
    QVERIFY(fuzzyEqual(viewer->visibleImageRect().topLeft(), before.topLeft() - QPointF(100, 40) * dpr));
    // Moving the mouse afterwards does not drag on.
    mouse(viewer, QEvent::MouseMove, Qt::NoButton, middle);
    QVERIFY(fuzzyEqual(viewer->visibleImageRect().topLeft(), before.topLeft() - QPointF(100, 40) * dpr));

    // Not beyond the edge of the image.
    viewer->pan(QPointF(100000, 100000));
    QVERIFY(fuzzyEqual(viewer->visibleImageRect().topLeft(), QPointF(0, 0)));
    viewer->pan(QPointF(-100000, -100000));
    QVERIFY(fuzzyEqual(viewer->visibleImageRect().bottomRight(), QPointF(4000, 3000)));
}

void WhaleViewerTest::testPagingFitsAgain()
{
    WhaleViewer *viewer = openImage(QStringLiteral("large.png"), largeImage());
    QVERIFY(viewer);
    QTest::keyClick(viewer, Qt::Key_1);
    QVERIFY(viewer->isZoomed());
    QTest::keyClick(viewer, Qt::Key_Home);
    QVERIFY(!viewer->isZoomed());
    QTRY_VERIFY(viewer->isLoaded());
    // A small image (40 × 30) at 100 % fits already: zooming out goes no further.
    QCOMPARE(viewer->zoom(), 1.0);
    QTest::keyClick(viewer, Qt::Key_Minus);
    QVERIFY(!viewer->isZoomed());
    QCOMPARE(viewer->zoom(), 1.0);
    QTest::keyClick(viewer, Qt::Key_Plus);
    QVERIFY(viewer->isZoomed());
}

static QSet<QRgb> colorsInTheMiddle(QWidget *widget)
{
    const QImage shot = widget->grab(QRect(widget->width() / 2 - 20, widget->height() / 2 - 20, 40, 40)).toImage();
    QSet<QRgb> colors;
    for (int y = 0; y < shot.height(); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            colors.insert(shot.pixel(x, y));
        }
    }
    return colors;
}

void WhaleViewerTest::testZoomSharpOrSmooth()
{
    // A checkerboard of single pixels at 800 %: sharp, only black and white (V-E13).
    QImage checker(40, 30, QImage::Format_RGB32);
    for (int y = 0; y < checker.height(); ++y) {
        for (int x = 0; x < checker.width(); ++x) {
            checker.setPixel(x, y, (x + y) % 2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
        }
    }
    WhaleViewer *viewer = openImage(QStringLiteral("checker.png"), checker);
    QVERIFY(viewer);
    viewer->setZoom(8, QPointF(viewer->width() / 2.0, viewer->height() / 2.0));
    QCOMPARE(colorsInTheMiddle(viewer), (QSet<QRgb>{qRgb(0, 0, 0), qRgb(255, 255, 255)}));
    viewer->close();
    QTRY_VERIFY(!WhaleViewer::instance());

    // Smooth: grey in between.
    WhaleViewerSettings::setSmoothZoom(true);
    viewer = openByActivation(QStringLiteral("checker.png"));
    QVERIFY(viewer);
    QTRY_VERIFY(viewer->isLoaded());
    viewer->setZoom(8, QPointF(viewer->width() / 2.0, viewer->height() / 2.0));
    QVERIFY(colorsInTheMiddle(viewer).count() > 2);
}

// A 4 × 4 GIF of three frames (red, blue, green), 30 ms each, looping.
static QByteArray animatedGif()
{
    static const char data[] =
        "\x47\x49\x46\x38\x39\x61\x04\x00\x04\x00\x81\x00\x00\xff\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x21\xff\x0b\x4e\x45\x54\x53"
        "\x43\x41\x50\x45\x32\x2e\x30\x03\x01\x00\x00\x00\x21\xf9\x04\x00"
        "\x03\x00\x00\x00\x2c\x00\x00\x00\x00\x04\x00\x04\x00\x00\x08\x09"
        "\x00\x01\x08\x1c\x48\xb0\x20\x80\x80\x00\x21\xf9\x04\x01\x03\x00"
        "\x01\x00\x2c\x00\x00\x00\x00\x04\x00\x04\x00\x81\x00\x00\xff\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x08\x09\x00\x01\x08\x1c\x48\xb0"
        "\x20\x80\x80\x00\x21\xf9\x04\x01\x03\x00\x01\x00\x2c\x00\x00\x00"
        "\x00\x04\x00\x04\x00\x81\x00\xff\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x08\x09\x00\x01\x08\x1c\x48\xb0\x20\x80\x80\x00\x3b";
    return QByteArray(data, sizeof(data) - 1);
}

void WhaleViewerTest::testAnimation()
{
    WhaleViewer *viewer = openFile(QStringLiteral("anim.gif"), animatedGif());
    QVERIFY(viewer);
    QVERIFY(viewer->cache().entry(url(QStringLiteral("anim.gif")))->animated);
    QTRY_VERIFY(viewer->isAnimating());
    // All three frames, then round again.
    QTRY_COMPARE(viewer->animationFrame(), 2);
    QTRY_COMPARE(viewer->animationFrame(), 0);
    QTRY_COMPARE(viewer->animationFrame(), 1);
    QVERIFY(viewer->isAnimating());

    // Paging on stops it; a still image does not play.
    QTest::keyClick(viewer, Qt::Key_Home);
    QVERIFY(!viewer->isAnimating());
    QCOMPARE(viewer->animationFrame(), -1);
    QTRY_VERIFY(viewer->isLoaded());
    QVERIFY(!viewer->cache().entry(url(QStringLiteral("1.png")))->animated);
    QTest::qWait(100);
    QVERIFY(!viewer->isAnimating());
}

void WhaleViewerTest::testVideo()
{
#if HAVE_MPV
    WhaleViewer *viewer = openFile(QStringLiteral("clip.webm"), whaleTestVideo());
    QVERIFY(viewer);
    QVERIFY(viewer->isVideo());
    QCOMPARE(viewer->overlayText(), QStringLiteral("clip.webm"));
    WhaleVideoPlayer *player = viewer->videoPlayer();
    QVERIFY(player);
    QCOMPARE(player->url(), url(QStringLiteral("clip.webm")));
    // The view follows as with images (V-E8).
    QTRY_COMPARE(m_view->currentItemUrl(), url(QStringLiteral("clip.webm")));
    // Plays at once, with sound, over and over (V-E16).
    QVERIFY(!player->isPaused());
    QVERIFY(!player->isMuted());
    QTRY_VERIFY(player->position() > 0.2);

    // A click between the edges pauses and plays (V-E19); the edges still page.
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, viewer->rect().center());
    QTRY_VERIFY(player->isPaused());
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, viewer->rect().center());
    QTRY_VERIFY(!player->isPaused());
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("clip.webm")));

    // Space, M (V-E17).
    QTest::keyClick(viewer, Qt::Key_Space);
    QTRY_VERIFY(player->isPaused());
    QTest::keyClick(viewer, Qt::Key_M);
    QTRY_VERIFY(player->isMuted());
    // Left and Right seek; they do not page.
    player->seekTo(1.5);
    QTRY_VERIFY(player->position() > 1.4);
    QTest::keyClick(viewer, Qt::Key_Left);
    QTRY_VERIFY(player->position() < 0.1);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("clip.webm")));
    // No zoom for videos.
    QTest::keyClick(viewer, Qt::Key_1);
    QVERIFY(!viewer->isZoomed());

    // Up pages; the video stops.
    QTest::keyClick(viewer, Qt::Key_Up);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("broken.png")));
    QVERIFY(!viewer->isVideo());
    QVERIFY(player->url().isEmpty());
    // And back: it plays again.
    QTest::keyClick(viewer, Qt::Key_Down);
    QTRY_VERIFY(viewer->isLoaded());
    QCOMPARE(player->url(), url(QStringLiteral("clip.webm")));
    QTRY_VERIFY(player->position() > 0.2);

    // Escape: closed, the video selected.
    QTest::keyClick(viewer, Qt::Key_Escape);
    QTRY_VERIFY(!WhaleViewer::instance());
    QCOMPARE(selectedUrls(), QList<QUrl>{url(QStringLiteral("clip.webm"))});
#else
    QVERIFY(!WhaleViewerSettings::videoSupported());
    QSKIP("built without libmpv");
#endif
}

void WhaleViewerTest::testVideoError()
{
#if HAVE_MPV
    m_testDir->createFile(QStringLiteral("bad.webm"), "not a video");
    QTRY_COMPARE(m_view->itemsCount(), 10);
    WhaleViewer *viewer = openByActivation(QStringLiteral("bad.webm"));
    QVERIFY(viewer);
    QVERIFY(viewer->isVideo());
    // As with images (V-E11).
    QTRY_VERIFY(viewer->hasLoadError());
    QVERIFY(!viewer->isLoaded());
    // On to an image (7.png; bad.webm comes before broken.png).
    QTest::keyClick(viewer, Qt::Key_Up);
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("7.png")));
    QTRY_VERIFY(viewer->isLoaded());
    QVERIFY(!viewer->hasLoadError());
#else
    QSKIP("built without libmpv");
#endif
}

void WhaleViewerTest::testVideoSettings()
{
#if HAVE_MPV
    WhaleViewerSettings::setVideoAutoplay(false);
    WhaleViewerSettings::setVideoSound(false);
    WhaleViewerSettings::setVideoLoop(false);
    WhaleViewer *viewer = openFile(QStringLiteral("clip.webm"), whaleTestVideo());
    QVERIFY(viewer);
    WhaleVideoPlayer *player = viewer->videoPlayer();
    QTRY_VERIFY(player->isPaused());
    QTRY_VERIFY(player->isMuted());
    QCOMPARE(player->options().loop, false);
    QTest::qWait(200);
    QVERIFY(player->position() < 0.05);
#else
    QSKIP("built without libmpv");
#endif
}

void WhaleViewerTest::testVideoControls()
{
#if HAVE_MPV
    WhaleViewer *viewer = openFile(QStringLiteral("clip.webm"), whaleTestVideo());
    QVERIFY(viewer);
    WhaleVideoPlayer *player = viewer->videoPlayer();
    const QPoint middle = viewer->rect().center();
    auto move = [viewer](const QPoint &position, Qt::MouseButtons buttons) {
        QMouseEvent event(QEvent::MouseMove, position, viewer->mapToGlobal(position), Qt::NoButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(viewer, &event);
    };
    // Where a fraction of the video is in the progress.
    auto at = [viewer](qreal fraction) {
        const QRect area = viewer->seekAreaRect();
        const int line = viewer->fontMetrics().height();
        return QPoint(area.left() + line / 2 + qRound(fraction * (area.width() - line)), area.center().y());
    };

    // With the pointer moving; they hide with it while the video plays (V-E18).
    move(middle, Qt::NoButton);
    QVERIFY(viewer->controlsVisible());
    QVERIFY(viewer->controlBarRect().bottom() < viewer->height());
    QTRY_VERIFY_WITH_TIMEOUT(!viewer->controlsVisible(), 4000);
    // Paused, they stay.
    QTest::keyClick(viewer, Qt::Key_Space);
    QTRY_VERIFY(viewer->controlsVisible());

    // The button plays and pauses.
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, viewer->playButtonRect().center());
    QTRY_VERIFY(!player->isPaused());
    move(middle, Qt::NoButton);
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, viewer->playButtonRect().center());
    QTRY_VERIFY(player->isPaused());

    // A click into the progress seeks there.
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, at(0.75));
    QTRY_VERIFY(qAbs(player->position() - 1.5) < 0.2);
    // Dragging seeks along.
    QTest::mousePress(viewer, Qt::LeftButton, Qt::NoModifier, at(0.25));
    QTRY_VERIFY(qAbs(player->position() - 0.5) < 0.2);
    move(at(0.5), Qt::LeftButton);
    QTRY_VERIFY(qAbs(player->position() - 1.0) < 0.2);
    QTest::mouseRelease(viewer, Qt::LeftButton, Qt::NoModifier, at(0.5));

    // Clicks on the bar do not page, also at the edges.
    const QRect bar = viewer->controlBarRect();
    QTest::mouseClick(viewer, Qt::LeftButton, Qt::NoModifier, QPoint(bar.right() - 2, bar.center().y()));
    QCOMPARE(viewer->currentUrl(), url(QStringLiteral("clip.webm")));

    // Images have none.
    QTest::keyClick(viewer, Qt::Key_Home);
    QTRY_VERIFY(viewer->isLoaded());
    move(middle, Qt::NoButton);
    QVERIFY(!viewer->controlsVisible());
#else
    QSKIP("built without libmpv");
#endif
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
