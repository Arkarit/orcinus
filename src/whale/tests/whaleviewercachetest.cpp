/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The images of the built-in viewer, decoded and scaled in the background (V2a,
 * .privatedoc/design-notes.md): fitting, errors, scaling again, the memory budget,
 * forgetting files, images larger than Qt allows by default. Colour profiles and enlarging small
 * images (V2b).
 */

#include "whaleviewercache.h"
#include "testdir.h"

#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTest>

class WhaleViewerCacheTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testFittedSize();
    void testDecodeAndScale();
    void testError();
    void testScaleAgainWithoutDecoding();
    void testBudget();
    void testRetainOnly();
    void testLargeImage();
    void testUpscale();
    void testColorProfileToSRgb();
    void testRequestFull();

private:
    QUrl createImage(const QString &name, const QSize &size);

    TestDir *m_testDir = nullptr;
};

QUrl WhaleViewerCacheTest::createImage(const QString &name, const QSize &size)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString path = m_testDir->path() + QLatin1Char('/') + name;
    if (!image.save(path, "png")) {
        return QUrl();
    }
    return QUrl::fromLocalFile(path);
}

void WhaleViewerCacheTest::init()
{
    m_testDir = new TestDir();
}

void WhaleViewerCacheTest::cleanup()
{
    delete m_testDir;
    m_testDir = nullptr;
}

void WhaleViewerCacheTest::testFittedSize()
{
    // Smaller images keep their size, larger ones fit with their aspect ratio.
    QCOMPARE(WhaleViewerCache::fittedSize(QSize(40, 30), QSize(800, 600)), QSize(40, 30));
    QCOMPARE(WhaleViewerCache::fittedSize(QSize(4000, 3000), QSize(800, 800)), QSize(800, 600));
    QCOMPARE(WhaleViewerCache::fittedSize(QSize(1000, 4000), QSize(800, 800)), QSize(200, 800));
}

void WhaleViewerCacheTest::testDecodeAndScale()
{
    const QUrl url = createImage(QStringLiteral("a.png"), QSize(400, 300));
    WhaleViewerCache cache;
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.setCurrent(url);
    cache.request(url, QSize(200, 200), 2.0);
    QVERIFY(cache.isPending(url));
    QVERIFY(!cache.entry(url));
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(ready.first().first().toUrl(), url);
    QVERIFY(!cache.isPending(url));
    const WhaleViewerCache::Entry *entry = cache.entry(url);
    QVERIFY(entry);
    QVERIFY(!entry->error);
    QCOMPARE(entry->full.size(), QSize(400, 300));
    QCOMPARE(entry->scaled.size(), QSize(200, 150));
    QCOMPARE(entry->scaled.devicePixelRatio(), 2.0);
    QCOMPARE(entry->available, QSize(200, 200));

    // Asked again for the same size: nothing to do.
    cache.request(url, QSize(200, 200), 2.0);
    QVERIFY(!cache.isPending(url));
}

void WhaleViewerCacheTest::testError()
{
    m_testDir->createFile(QStringLiteral("broken.png"), "not an image");
    const QUrl url = QUrl::fromLocalFile(m_testDir->path() + QStringLiteral("/broken.png"));
    WhaleViewerCache cache;
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.request(url, QSize(200, 200), 1.0);
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(cache.entry(url));
    QVERIFY(cache.entry(url)->error);
    // A failed file is not tried again for another size.
    cache.request(url, QSize(300, 300), 1.0);
    QVERIFY(!cache.isPending(url));
}

void WhaleViewerCacheTest::testScaleAgainWithoutDecoding()
{
    const QUrl url = createImage(QStringLiteral("a.png"), QSize(400, 300));
    WhaleViewerCache cache;
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.setCurrent(url);
    cache.request(url, QSize(200, 200), 1.0);
    QTRY_COMPARE(ready.count(), 1);
    // The file is gone, yet another window size works: the full image is kept.
    QVERIFY(QFile::remove(url.toLocalFile()));
    cache.request(url, QSize(100, 100), 1.0);
    QTRY_COMPARE(ready.count(), 2);
    QVERIFY(!cache.entry(url)->error);
    QCOMPARE(cache.entry(url)->scaled.size(), QSize(100, 75));
}

void WhaleViewerCacheTest::testBudget()
{
    const QUrl current = createImage(QStringLiteral("a.png"), QSize(400, 300));
    const QUrl neighbour = createImage(QStringLiteral("b.png"), QSize(400, 300));
    WhaleViewerCache cache;
    cache.setFullImageBudget(0);
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.setCurrent(current);
    cache.request(current, QSize(200, 200), 1.0);
    cache.request(neighbour, QSize(200, 200), 1.0);
    QTRY_COMPARE(ready.count(), 2);
    // Only the current file keeps its full image; the neighbour keeps the scaled one.
    QVERIFY(!cache.entry(current)->full.isNull());
    QVERIFY(cache.entry(neighbour)->full.isNull());
    QCOMPARE(cache.entry(neighbour)->scaled.size(), QSize(200, 150));
    QVERIFY(!cache.entry(neighbour)->error);
}

void WhaleViewerCacheTest::testRetainOnly()
{
    const QUrl a = createImage(QStringLiteral("a.png"), QSize(400, 300));
    const QUrl b = createImage(QStringLiteral("b.png"), QSize(400, 300));
    const QUrl c = createImage(QStringLiteral("c.png"), QSize(400, 300));
    WhaleViewerCache cache;
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.request(a, QSize(200, 200), 1.0);
    QTRY_COMPARE(ready.count(), 1);
    cache.request(b, QSize(200, 200), 1.0);
    cache.request(c, QSize(200, 200), 1.0);
    cache.retainOnly({c});
    QVERIFY(!cache.entry(a));
    QVERIFY(!cache.isPending(b));
    QTRY_VERIFY(cache.entry(c));
    // b is never announced.
    QTest::qWait(50);
    QCOMPARE(ready.count(), 2);
    QCOMPARE(ready.last().first().toUrl(), c);
    QVERIFY(!cache.entry(b));
}

void WhaleViewerCacheTest::testLargeImage()
{
    // 8200 × 8200 in RGB32 is over the 256 MB Qt allows by default.
    const QUrl url = createImage(QStringLiteral("large.png"), QSize(8200, 8200));
    QVERIFY(url.isValid());
    WhaleViewerCache cache;
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.setCurrent(url);
    cache.request(url, QSize(800, 800), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);
    QVERIFY(!cache.entry(url)->error);
    QCOMPARE(cache.entry(url)->scaled.size(), QSize(800, 800));
}

void WhaleViewerCacheTest::testUpscale()
{
    QCOMPARE(WhaleViewerCache::fittedSize(QSize(40, 30), QSize(800, 800), true), QSize(800, 600));
    QCOMPARE(WhaleViewerCache::fittedSize(QSize(4000, 3000), QSize(800, 800), true), QSize(800, 600));

    const QUrl url = createImage(QStringLiteral("small.png"), QSize(40, 30));
    WhaleViewerCache cache;
    cache.setUpscale(true);
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.request(url, QSize(200, 200), 1.0);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(cache.entry(url)->scaled.size(), QSize(200, 150));
}

void WhaleViewerCacheTest::testColorProfileToSRgb()
{
    // A Display P3 image: its green is not sRGB's green.
    QImage image(20, 20, QImage::Format_RGB32);
    image.fill(QColor(40, 180, 60));
    image.setColorSpace(QColorSpace::DisplayP3);
    const QString path = m_testDir->path() + QStringLiteral("/p3.png");
    QVERIFY(image.save(path, "png"));
    const QUrl url = QUrl::fromLocalFile(path);
    {
        // Qt writes the profile along.
        QImage reread(path);
        QCOMPARE(reread.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
    }

    WhaleViewerCache cache;
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.request(url, QSize(200, 200), 1.0);
    QTRY_COMPARE(ready.count(), 1);
    const QImage full = cache.entry(url)->full;
    QCOMPARE(full.colorSpace(), QColorSpace(QColorSpace::SRgb));
    const QColor expected = image.convertedToColorSpace(QColorSpace::SRgb).pixelColor(0, 0);
    QVERIFY(expected != QColor(40, 180, 60));
    QCOMPARE(full.pixelColor(0, 0), expected);
    QCOMPARE(cache.entry(url)->scaled.pixelColor(0, 0), expected);
}

void WhaleViewerCacheTest::testRequestFull()
{
    const QUrl a = createImage(QStringLiteral("a.png"), QSize(400, 300));
    const QUrl b = createImage(QStringLiteral("b.png"), QSize(400, 300));
    WhaleViewerCache cache;
    cache.setFullImageBudget(0);
    QSignalSpy ready(&cache, &WhaleViewerCache::imageReady);
    cache.setCurrent(a);
    cache.request(a, QSize(200, 200), 1.0);
    cache.request(b, QSize(200, 200), 1.0);
    QTRY_COMPARE(ready.count(), 2);
    QVERIFY(cache.entry(b)->full.isNull());
    QCOMPARE(cache.entry(b)->fullSize, QSize(400, 300));

    // b becomes the current file and is zoomed into: its full image is decoded again, and a,
    // no longer current, drops its own.
    cache.setCurrent(b);
    QVERIFY(cache.entry(a)->full.isNull());
    cache.requestFull(b, 1.0);
    QVERIFY(cache.isPending(b));
    QTRY_COMPARE(ready.count(), 3);
    QCOMPARE(cache.entry(b)->full.size(), QSize(400, 300));
    QCOMPARE(cache.entry(b)->scaled.size(), QSize(200, 150));
    // Nothing to do once it is there.
    cache.requestFull(b, 1.0);
    QVERIFY(!cache.isPending(b));
}

QTEST_MAIN(WhaleViewerCacheTest)

#include "whaleviewercachetest.moc"
