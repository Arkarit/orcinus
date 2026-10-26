/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "treemaplayout.h"
#include "treemaprenderer.h"
#include "whaletreenode_p.h"

#include <QElapsedTimer>
#include <QIcon>
#include <QPixmap>
#include <QRandomGenerator>
#include <QTest>

#include <cmath>

namespace
{
/// A tree described in a test; realized as WhaleTreeNodes like the scanner makes them.
struct Spec {
    QByteArray name;
    qint64 size = 0;
    QList<Spec> children;
    bool directory = false;
};

Spec file(const QByteArray &name, qint64 size)
{
    return Spec{name, size, {}, false};
}

Spec dir(const QByteArray &name, const QList<Spec> &children)
{
    return Spec{name, 0, children, true};
}

WhaleTreeNode realize(const Spec &spec, const QByteArray &parentPath, int depth)
{
    auto data = new WhaleTreeNodeData;
    data->parentPath = parentPath;
    data->name = spec.name;
    data->depth = depth;
    data->type = spec.directory ? WhaleTreeNode::Type::Directory : WhaleTreeNode::Type::File;
    data->size = spec.directory ? 0 : spec.size;
    data->totalSize = data->size;
    const QByteArray path = data->encodedPath();
    // Children sorted by name, as the scanner delivers them.
    QList<Spec> children = spec.children;
    std::sort(children.begin(), children.end(), [](const Spec &a, const Spec &b) {
        return a.name < b.name;
    });
    for (const Spec &child : std::as_const(children)) {
        const WhaleTreeNode node = realize(child, path, depth + 1);
        data->children.append(node);
        data->totalSize += node.totalSize();
        if (node.isDirectory()) {
            data->directoryCount += 1 + node.directoryCount();
            data->fileCount += node.fileCount();
        } else {
            data->fileCount += 1;
        }
    }
    return WhaleTreeNode(QExplicitlySharedDataPointer<const WhaleTreeNodeData>(data));
}

WhaleTreeNode tree(const QList<Spec> &children)
{
    return realize(dir("/root", children), QByteArray(), 0);
}

/// A folder tree with @p files files of very unequal sizes in folders up to @p depth deep.
WhaleTreeNode randomTree(int files, int depth, int fanOut, quint32 seed)
{
    QRandomGenerator random(seed);
    std::function<Spec(const QByteArray &, int, int)> make = [&](const QByteArray &name, int level, int budget) {
        Spec folder = dir(name, {});
        int filesHere = level == depth ? budget : budget / (fanOut + 1);
        for (int i = 0; i < filesHere; ++i) {
            // Pareto-like: many small files, a few big ones.
            const double u = std::max(1e-9, random.generateDouble());
            folder.children.append(file("f" + QByteArray::number(i), qint64(1000.0 / std::pow(u, 0.8))));
        }
        if (level < depth) {
            const int rest = budget - filesHere;
            for (int i = 0; i < fanOut; ++i) {
                folder.children.append(make("d" + QByteArray::number(i), level + 1, rest / fanOut));
            }
        }
        return folder;
    };
    return realize(make("/random", 0, files), QByteArray(), 0);
}

double area(const QRectF &rect)
{
    return rect.width() * rect.height();
}

bool finite(const QRectF &rect)
{
    return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width()) && std::isfinite(rect.height());
}

/// Every pixel of @p bounds (by its center) lies in exactly one leaf.
bool tilesPixels(const TreemapLayout &layout, QString *error)
{
    const QRect pixels = layout.bounds().toRect();
    QList<int> count(pixels.width() * pixels.height(), 0);
    for (const TreemapLayout::Cell &cell : layout.cells()) {
        if (!cell.isLeaf()) {
            continue;
        }
        const int left = std::max(pixels.left(), int(std::ceil(cell.rect.left() - 0.5)));
        const int right = std::min(pixels.left() + pixels.width(), int(std::ceil(cell.rect.right() - 0.5)));
        const int top = std::max(pixels.top(), int(std::ceil(cell.rect.top() - 0.5)));
        const int bottom = std::min(pixels.top() + pixels.height(), int(std::ceil(cell.rect.bottom() - 0.5)));
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                ++count[(y - pixels.top()) * pixels.width() + (x - pixels.left())];
            }
        }
    }
    for (int i = 0; i < count.count(); ++i) {
        if (count.at(i) != 1) {
            *error = QStringLiteral("pixel %1,%2 is covered %3 times").arg(i % pixels.width()).arg(i / pixels.width()).arg(count.at(i));
            return false;
        }
    }
    return true;
}
}

class WhaleTreemapLayoutTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testSquarify();
    void testFillsBounds();
    void testNoOverlap();
    void testValidGeometry();
    void testProportional();
    void testDeterministic();
    void testDeepHierarchy();
    void testZeroSizes();
    void testTinyNodes();
    void testResize();
    void testHitTest();
    void testLargeTree_data();
    void testLargeTree();
    void testRenderer();
    void testRestCells();
};

void WhaleTreemapLayoutTest::testSquarify()
{
    // The example of Bruls et al.: sizes 6, 6, 4, 3, 2, 2, 1 in a 6x4 rectangle.
    const QList<QRectF> rects = TreemapLayout::squarify({6, 6, 4, 3, 2, 2, 1}, QRectF(0, 0, 6, 4));
    QCOMPARE(rects.count(), 7);
    double total = 0;
    double worst = 0;
    for (const QRectF &rect : rects) {
        total += area(rect);
        worst = std::max(worst, std::max(rect.width() / rect.height(), rect.height() / rect.width()));
    }
    QVERIFY(std::abs(total - 24) < 1e-9);
    // Squarified keeps the worst aspect ratio low; slicing the rectangle into strips would reach 24.
    QVERIFY2(worst <= 3, qPrintable(QString::number(worst)));
    QVERIFY(std::abs(area(rects.at(0)) - 6) < 1e-9);
    QVERIFY(std::abs(area(rects.at(6)) - 1) < 1e-9);

    // On average, squarified rectangles of random sizes stay close to squares.
    QRandomGenerator random(5);
    QList<qint64> sizes;
    for (int i = 0; i < 500; ++i) {
        sizes.append(1 + random.bounded(100000));
    }
    std::sort(sizes.begin(), sizes.end(), std::greater<qint64>());
    const QList<QRectF> many = TreemapLayout::squarify(sizes, QRectF(0, 0, 1600, 1000));
    double ratios = 0;
    for (const QRectF &rect : many) {
        ratios += std::max(rect.width() / rect.height(), rect.height() / rect.width());
    }
    QVERIFY2(ratios / many.count() < 2.0, qPrintable(QString::number(ratios / many.count())));
}

void WhaleTreemapLayoutTest::testFillsBounds()
{
    const WhaleTreeNode root = randomTree(3000, 3, 4, 1);
    TreemapLayout layout;
    TreemapLayout::Options options;
    options.minCellArea = 0; // every entry gets a cell
    options.minDivideSide = 0;
    layout.compute(root, QRectF(0, 0, 640, 400), options);
    QVERIFY(!layout.cells().isEmpty());
    QCOMPARE(layout.cells().first().rect, QRectF(0, 0, 640, 400));

    double leafArea = 0;
    for (const TreemapLayout::Cell &cell : layout.cells()) {
        if (cell.isLeaf()) {
            leafArea += area(cell.rect);
        }
    }
    QVERIFY2(std::abs(leafArea - 640.0 * 400.0) < 1e-6 * 640 * 400, qPrintable(QString::number(leafArea)));

    // With the default options too: the leaves (entries, rest cells, undivided folders)
    // cover every pixel exactly once.
    layout.compute(root, QRectF(0, 0, 640, 400));
    QString error;
    QVERIFY2(tilesPixels(layout, &error), qPrintable(error));
}

void WhaleTreemapLayoutTest::testNoOverlap()
{
    const WhaleTreeNode root = randomTree(600, 2, 3, 2);
    TreemapLayout layout;
    layout.compute(root, QRectF(10, 20, 500, 300));
    const QList<TreemapLayout::Cell> &cells = layout.cells();
    QVERIFY(cells.count() > 50);
    for (int i = 0; i < cells.count(); ++i) {
        const TreemapLayout::Cell &cell = cells.at(i);
        // Inside the parent (up to rounding)...
        if (cell.parent >= 0) {
            const QRectF parent = cells.at(cell.parent).rect.adjusted(-1e-9, -1e-9, 1e-9, 1e-9);
            QVERIFY(parent.contains(cell.rect));
            QVERIFY(cell.depth == cells.at(cell.parent).depth + 1);
        }
        // ... and apart from the siblings.
        for (int sibling = cell.firstChild; sibling >= 0 && sibling < cell.firstChild + cell.childCount; ++sibling) {
            for (int other = sibling + 1; other < cell.firstChild + cell.childCount; ++other) {
                const QRectF overlap = cells.at(sibling).rect.intersected(cells.at(other).rect);
                QVERIFY2(overlap.isEmpty() || area(overlap) < 1e-6,
                         qPrintable(QStringLiteral("cells %1 and %2 overlap by %3").arg(sibling).arg(other).arg(area(overlap))));
            }
        }
    }
    QString error;
    QVERIFY2(tilesPixels(layout, &error), qPrintable(error));
}

void WhaleTreemapLayoutTest::testValidGeometry()
{
    // Extreme ratios of sizes and bounds.
    const WhaleTreeNode root = tree({file("huge", qint64(1) << 50), file("tiny", 1), dir("d", {file("x", 1), file("y", qint64(1) << 40)})});
    for (const QRectF &bounds : {QRectF(0, 0, 3000, 2), QRectF(0, 0, 2, 3000), QRectF(0, 0, 1, 1), QRectF(0, 0, 1e6, 1e6)}) {
        TreemapLayout layout;
        layout.compute(root, bounds);
        for (const TreemapLayout::Cell &cell : layout.cells()) {
            QVERIFY(finite(cell.rect));
            QVERIFY(cell.rect.width() >= 0);
            QVERIFY(cell.rect.height() >= 0);
            QVERIFY(cell.size > 0);
        }
    }
    // Empty or invalid bounds: no cells, no crash.
    TreemapLayout layout;
    layout.compute(root, QRectF());
    QVERIFY(layout.cells().isEmpty());
    layout.compute(root, QRectF(0, 0, -10, 5));
    QVERIFY(layout.cells().isEmpty());
    layout.compute(root, QRectF(0, 0, qQNaN(), 5));
    QVERIFY(layout.cells().isEmpty());
    layout.compute(WhaleTreeNode(), QRectF(0, 0, 10, 10));
    QVERIFY(layout.cells().isEmpty());
}

void WhaleTreemapLayoutTest::testProportional()
{
    const WhaleTreeNode root = randomTree(2000, 3, 3, 3);
    TreemapLayout layout;
    layout.compute(root, QRectF(0, 0, 1200, 800));
    const QList<TreemapLayout::Cell> &cells = layout.cells();
    int checked = 0;
    for (const TreemapLayout::Cell &parent : cells) {
        if (parent.childCount == 0) {
            continue;
        }
        qint64 total = 0;
        for (int child = parent.firstChild; child < parent.firstChild + parent.childCount; ++child) {
            total += cells.at(child).size;
        }
        // Folders have no size of their own here, so the children stand for all of it.
        QCOMPARE(total, parent.size);
        for (int child = parent.firstChild; child < parent.firstChild + parent.childCount; ++child) {
            const double expected = double(cells.at(child).size) / double(total);
            const double actual = area(cells.at(child).rect) / area(parent.rect);
            QVERIFY2(std::abs(expected - actual) < 1e-9 + 1e-6 * expected, qPrintable(QStringLiteral("%1 instead of %2").arg(actual).arg(expected)));
            ++checked;
        }
    }
    QVERIFY(checked > 100);
    // And against the whole: a cell's share of the bounds is its share of the bytes.
    for (const TreemapLayout::Cell &cell : cells) {
        const double expected = double(cell.size) / double(root.totalSize());
        QVERIFY(std::abs(area(cell.rect) / (1200.0 * 800.0) - expected) < 1e-6);
    }
}

void WhaleTreemapLayoutTest::testDeterministic()
{
    // Equal sizes: the name order decides, in each run and for a tree made again.
    QList<Spec> children;
    for (int i = 0; i < 50; ++i) {
        children.append(file("same" + QByteArray::number(i), 100));
        children.append(dir("dir" + QByteArray::number(i), {file("a", 10 + i), file("b", 10 + i)}));
    }
    TreemapLayout first;
    first.compute(tree(children), QRectF(0, 0, 800, 600));
    for (int run = 0; run < 3; ++run) {
        TreemapLayout again;
        again.compute(tree(children), QRectF(0, 0, 800, 600));
        QCOMPARE(again.cells().count(), first.cells().count());
        for (int i = 0; i < first.cells().count(); ++i) {
            QCOMPARE(again.cells().at(i).rect, first.cells().at(i).rect);
            QCOMPARE(again.cells().at(i).node.path(), first.cells().at(i).node.path());
        }
    }
    // Largest first (dir49 with 2 x 59 bytes), equal sizes (100: dir40 and the files) by name.
    const TreemapLayout::Cell &root = first.cells().first();
    QCOMPARE(first.cells().at(root.firstChild).node.name(), QStringLiteral("dir49"));
    QStringList hundred;
    for (int i = root.firstChild; i < root.firstChild + root.childCount; ++i) {
        if (first.cells().at(i).size == 100) {
            hundred.append(first.cells().at(i).node.name());
        }
    }
    QCOMPARE(hundred.count(), 51);
    QCOMPARE(hundred.mid(0, 3), QStringList({QStringLiteral("dir40"), QStringLiteral("same0"), QStringLiteral("same1")}));
    QCOMPARE(hundred.last(), QStringLiteral("same9"));
}

void WhaleTreemapLayoutTest::testDeepHierarchy()
{
    // A chain of 60 folders, each with a file as large as everything below it, so the
    // area halves with every level.
    Spec chain = dir("level60", {file("leaf", 1)});
    qint64 below = 1;
    for (int level = 59; level >= 1; --level) {
        chain = dir("level" + QByteArray::number(level), {chain, file("file", below)});
        below *= 2;
    }
    const WhaleTreeNode root = tree({chain});
    TreemapLayout layout;
    TreemapLayout::Options options;
    options.minCellArea = 0;
    options.minDivideSide = 0;
    layout.compute(root, QRectF(0, 0, 1000, 1000), options);
    int deepest = 0;
    for (const TreemapLayout::Cell &cell : layout.cells()) {
        deepest = std::max(deepest, cell.depth);
        QVERIFY(finite(cell.rect));
    }
    QCOMPARE(deepest, 61); // root, 60 folders, the leaf

    // With the default options, the division stops where cells become too small.
    layout.compute(root, QRectF(0, 0, 1000, 1000));
    deepest = 0;
    for (const TreemapLayout::Cell &cell : layout.cells()) {
        deepest = std::max(deepest, cell.depth);
        if (!cell.isLeaf()) {
            QVERIFY(cell.rect.width() >= TreemapLayout::Options().minDivideSide);
            QVERIFY(cell.rect.height() >= TreemapLayout::Options().minDivideSide);
        }
    }
    QVERIFY(deepest > 5);
    QVERIFY(deepest < 61);
    // The deepest folder reachable is found by its path.
    const int found = layout.cellFor(QStringLiteral("/root/level1/level2/level3"));
    QVERIFY(found > 0);
    QCOMPARE(layout.cells().at(found).node.name(), QStringLiteral("level3"));
    // A path below what is shown resolves to the deepest shown folder above it.
    QString deepPath = QStringLiteral("/root");
    for (int level = 1; level <= 60; ++level) {
        deepPath += QStringLiteral("/level") + QString::number(level);
    }
    const int deepCell = layout.cellFor(deepPath + QStringLiteral("/leaf"));
    QVERIFY(deepCell > 0);
    QVERIFY(layout.cells().at(deepCell).isLeaf());
    QVERIFY(deepPath.startsWith(layout.cells().at(deepCell).node.path()));
    QCOMPARE(layout.cellFor(QStringLiteral("/elsewhere")), -1);
}

void WhaleTreemapLayoutTest::testZeroSizes()
{
    const WhaleTreeNode root = tree({file("empty", 0),
                                     dir("emptydir", {}),
                                     dir("zeros", {file("z1", 0), file("z2", 0)}),
                                     dir("mixed", {file("z", 0), file("data", 500)}),
                                     file("data", 1500)});
    TreemapLayout layout;
    layout.compute(root, QRectF(0, 0, 400, 300));
    QStringList names;
    for (const TreemapLayout::Cell &cell : layout.cells()) {
        QVERIFY(cell.size > 0);
        QVERIFY(area(cell.rect) > 0);
        names.append(cell.isRest() ? QStringLiteral("<rest>") : cell.node.name());
    }
    QCOMPARE(names, QStringList({QStringLiteral("root"), QStringLiteral("data"), QStringLiteral("mixed"), QStringLiteral("data")}));
    // The single entry of "mixed" with a size fills it.
    QCOMPARE(layout.cells().at(3).rect, layout.cells().at(2).rect);

    // Nothing with a size: no cells at all.
    layout.compute(tree({file("empty", 0), dir("emptydir", {})}), QRectF(0, 0, 400, 300));
    QVERIFY(layout.cells().isEmpty());
}

void WhaleTreemapLayoutTest::testTinyNodes()
{
    // One big file and 10 000 tiny ones: the tiny ones share one rest cell.
    QList<Spec> children{file("big", 1000000000)};
    for (int i = 0; i < 10000; ++i) {
        children.append(file("tiny" + QByteArray::number(i), 10));
    }
    const WhaleTreeNode root = tree(children);
    TreemapLayout layout;
    layout.compute(root, QRectF(0, 0, 800, 600));
    const QList<TreemapLayout::Cell> &cells = layout.cells();
    QCOMPARE(cells.count(), 3);
    const TreemapLayout::Cell &rest = cells.at(2);
    QVERIFY(rest.isRest());
    QCOMPARE(rest.restCount, 10000);
    QCOMPARE(rest.size, qint64(100000));
    // Still proportional: the rest cell has the area of all tiny files together.
    QVERIFY(std::abs(area(rest.rect) / (800.0 * 600.0) - 100000.0 / root.totalSize()) < 1e-9);
    QCOMPARE(layout.cellFor(QStringLiteral("/root/tiny5")), 0); // not shown on its own: the root

    // Visible ones stay: 100 files of a similar size all get cells.
    QList<Spec> similar;
    for (int i = 0; i < 100; ++i) {
        similar.append(file("s" + QByteArray::number(i), 1000 + i));
    }
    layout.compute(tree(similar), QRectF(0, 0, 800, 600));
    QCOMPARE(layout.cells().count(), 101);
}

void WhaleTreemapLayoutTest::testResize()
{
    const WhaleTreeNode root = randomTree(1500, 3, 3, 4);
    TreemapLayout small;
    small.compute(root, QRectF(0, 0, 300, 200));
    TreemapLayout large;
    large.compute(root, QRectF(0, 0, 1920, 1080));
    // A larger area shows more (and never fewer) cells; each one keeps its share.
    QVERIFY(large.cells().count() > small.cells().count());
    for (const TreemapLayout *layout : {&small, &large}) {
        const double total = area(layout->bounds());
        for (const TreemapLayout::Cell &cell : layout->cells()) {
            QVERIFY(std::abs(area(cell.rect) / total - double(cell.size) / root.totalSize()) < 1e-6);
        }
        QString error;
        QVERIFY2(tilesPixels(*layout, &error), qPrintable(error));
    }
    // A cell shown in both is found at its new place by path.
    const TreemapLayout::Cell &first = small.cells().at(small.cells().first().firstChild);
    const int again = large.cellFor(first.node.path());
    QVERIFY(again > 0);
    QCOMPARE(large.cells().at(again).node.path(), first.node.path());
    QVERIFY(large.cellAt(large.cells().at(again).rect.center()) >= again);
}

void WhaleTreemapLayoutTest::testHitTest()
{
    const WhaleTreeNode root = randomTree(800, 2, 4, 6);
    TreemapLayout layout;
    layout.compute(root, QRectF(0, 0, 700, 500));
    QRandomGenerator random(9);
    for (int i = 0; i < 2000; ++i) {
        const QPointF point(random.generateDouble() * 700, random.generateDouble() * 500);
        const int index = layout.cellAt(point);
        QVERIFY(index >= 0);
        const TreemapLayout::Cell &cell = layout.cells().at(index);
        QVERIFY(cell.isLeaf()); // the deepest cell
        QVERIFY(point.x() >= cell.rect.left() && point.x() < cell.rect.right());
        QVERIFY(point.y() >= cell.rect.top() && point.y() < cell.rect.bottom());
        // Its ancestors contain the point too.
        for (int parent = cell.parent; parent >= 0; parent = layout.cells().at(parent).parent) {
            QVERIFY(layout.cells().at(parent).rect.contains(point));
        }
    }
    QCOMPARE(layout.cellAt(QPointF(-1, 10)), -1);
    QCOMPARE(layout.cellAt(QPointF(700, 10)), -1);
    QCOMPARE(layout.cellAt(QPointF(10, 500)), -1);
}

void WhaleTreemapLayoutTest::testLargeTree_data()
{
    QTest::addColumn<int>("files");
    QTest::addColumn<int>("depth");
    QTest::newRow("1k") << 1000 << 4;
    QTest::newRow("10k") << 10000 << 4;
    QTest::newRow("100k") << 100000 << 4;
    QTest::newRow("100k in one folder") << 100000 << 0;
}

void WhaleTreemapLayoutTest::testLargeTree()
{
    QFETCH(int, files);
    QFETCH(int, depth);
    const WhaleTreeNode root = randomTree(files, depth, 6, 11);
    const QRectF bounds(0, 0, 1600, 1000);

    QElapsedTimer clock;
    clock.start();
    TreemapLayout layout;
    layout.compute(root, bounds);
    const qint64 layoutUs = clock.nsecsElapsed() / 1000;

    clock.restart();
    TreemapRenderer renderer;
    TreemapRenderer::Options options;
    const QImage image = renderer.render(layout, options, 1.0);
    const qint64 renderUs = clock.nsecsElapsed() / 1000;

    clock.restart();
    TreemapLayout resized;
    resized.compute(root, QRectF(0, 0, 1200, 900));
    const qint64 resizeUs = clock.nsecsElapsed() / 1000;

    int leaves = 0;
    int rests = 0;
    for (const TreemapLayout::Cell &cell : layout.cells()) {
        leaves += cell.isLeaf() ? 1 : 0;
        rests += cell.isRest() ? 1 : 0;
    }
    qInfo("%d files, %d levels: %d cells (%d leaves, %d rest cells), layout %lld µs, render %lld µs, layout after resize %lld µs",
          files,
          depth,
          int(layout.cells().count()),
          leaves,
          rests,
          layoutUs,
          renderUs,
          resizeUs);
    QVERIFY(!layout.isTruncated());
    QCOMPARE(image.size(), QSize(1600, 1000));
    // The number of cells is bounded by the area, not by the tree.
    QVERIFY(layout.cells().count() <= int(area(bounds) / TreemapLayout::Options().minCellArea) + 1000);
    QString error;
    QVERIFY2(tilesPixels(layout, &error), qPrintable(error));
    // Interactive even for the largest tree (generous for slow CI machines).
    QVERIFY2(layoutUs < 1000000, qPrintable(QString::number(layoutUs)));
}

void WhaleTreemapLayoutTest::testRenderer()
{
    const WhaleTreeNode root = tree({file("photo.jpg", 6000), file("song.flac", 3000), dir("docs", {file("a.pdf", 500), file("b.pdf", 500)})});
    TreemapLayout layout;
    layout.compute(root, QRectF(0, 0, 300, 200));
    TreemapRenderer renderer;
    TreemapRenderer::Options options;
    options.showLabels = false;
    options.palette.setColor(QPalette::Base, Qt::black);
    for (const qreal ratio : {1.0, 2.0}) {
        const QImage image = renderer.render(layout, options, ratio);
        QCOMPARE(image.size(), QSize(int(300 * ratio), int(200 * ratio)));
        QCOMPARE(image.devicePixelRatio(), ratio);
        // Every pixel is painted (the leaves tile the bounds), so none keeps the black background.
        for (int y = 0; y < image.height(); y += 7) {
            for (int x = 0; x < image.width(); x += 7) {
                QVERIFY2(image.pixel(x, y) != qRgb(0, 0, 0), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
            }
        }
        // The center of a cushion is lit, its hue is the file type's.
        const int photo = layout.cellFor(QStringLiteral("/root/photo.jpg"));
        const QColor center = QColor(image.pixel((layout.cells().at(photo).rect.center() * ratio).toPoint()));
        const QColor base = renderer.baseColor(layout, photo, options);
        QVERIFY(std::abs(center.hsvHue() - base.hsvHue()) <= 6);
        QVERIFY(center.value() > 120);
    }
    // File type colors: images and audio differ; in the top folder mode, everything below
    // one entry of the root shares its color, also when zoomed into that entry.
    const TreemapRenderer::Options fileType = options;
    TreemapRenderer::Options topFolder = options;
    topFolder.colorMode = TreemapRenderer::ColorMode::TopFolder;
    const int photo = layout.cellFor(QStringLiteral("/root/photo.jpg"));
    const int song = layout.cellFor(QStringLiteral("/root/song.flac"));
    QVERIFY(renderer.baseColor(layout, photo, fileType) != renderer.baseColor(layout, song, fileType));
    const int a = layout.cellFor(QStringLiteral("/root/docs/a.pdf"));
    const int b = layout.cellFor(QStringLiteral("/root/docs/b.pdf"));
    QCOMPARE(renderer.baseColor(layout, a, topFolder), renderer.baseColor(layout, b, topFolder));
    const QColor expectedDocs = renderer.baseColor(layout, a, topFolder);
    const int docs = layout.cellFor(QStringLiteral("/root/docs"));
    const int docsPosition = docs - layout.cells().first().firstChild;
    QCOMPARE(TreemapLayout::orderedChildren(root).at(docsPosition).name(), QStringLiteral("docs"));
    TreemapLayout zoomed;
    zoomed.compute(TreemapLayout::orderedChildren(root).at(docsPosition), QRectF(0, 0, 300, 200));
    topFolder.topFolderPosition = docsPosition;
    QCOMPARE(renderer.baseColor(zoomed, zoomed.cells().first().firstChild, topFolder), expectedDocs);

    // Nothing to show: an empty image of the background.
    TreemapLayout empty;
    empty.compute(WhaleTreeNode(), QRectF(0, 0, 50, 40));
    QCOMPARE(renderer.render(empty, options, 1.0).pixel(10, 10), qRgb(0, 0, 0));
}

void WhaleTreemapLayoutTest::testRestCells()
{
    // Many small files share a rest cell. It selects their folder, unlike a cushion of an
    // entry: it is flat and always carries the icon, also without names.
    QList<Spec> children{file("big", 200000)};
    for (int i = 0; i < 10000; ++i) {
        children.append(file("tiny" + QByteArray::number(i), 10));
    }
    TreemapLayout layout;
    layout.compute(tree(children), QRectF(0, 0, 400, 300));
    const auto rest = std::ranges::find_if(layout.cells(), &TreemapLayout::Cell::isRest);
    QVERIFY(rest != layout.cells().end());
    QVERIFY(rest->rect.width() >= 40 && rest->rect.height() >= 40);

    TreemapRenderer renderer;
    TreemapRenderer::Options options;
    options.showLabels = false;
    QPixmap solid(64, 64);
    solid.fill(Qt::black);
    options.restIcon = QIcon(solid);
    const QImage image = renderer.render(layout, options, 1.0);
    // The icon in white in the middle; around it one flat color.
    QCOMPARE(QColor(image.pixel(rest->rect.center().toPoint())), QColor(Qt::white));
    const QRgb flat = image.pixel((rest->rect.topLeft() + QPointF(2, 2)).toPoint());
    QVERIFY(flat != qRgb(255, 255, 255));
    QCOMPARE(image.pixel((rest->rect.bottomRight() - QPointF(3, 3)).toPoint()), flat);
    QCOMPARE(image.pixel((rest->rect.topRight() + QPointF(-3, 2)).toPoint()), flat);

    // Too small for the icon: just the flat cell.
    TreemapLayout small;
    small.compute(tree(children), QRectF(0, 0, 60, 40));
    const auto smallRest = std::ranges::find_if(small.cells(), &TreemapLayout::Cell::isRest);
    if (smallRest != small.cells().end() && std::min(smallRest->rect.width(), smallRest->rect.height()) < 12) {
        const QImage tiny = renderer.render(small, options, 1.0);
        QVERIFY(tiny.pixel(smallRest->rect.center().toPoint()) != qRgb(255, 255, 255));
    }
}

QTEST_MAIN(WhaleTreemapLayoutTest)

#include "whaletreemaplayouttest.moc"
