/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphin_compactmodesettings.h"
#include "kitemviews/kfileitemlistview.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "testdir.h"
#include "views/dolphinitemlistview.h"
#include "views/expansiontintsettings.h"

#include <QApplication>
#include <QGraphicsView>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

/**
 * Tinting the background of expanded folders in the compact view by depth (ET2).
 *
 * WHALE_TEST_SCREENSHOT_DIR=<dir> saves screenshots of all presets with a light
 * and a dark palette there (testScreenshots).
 */
class WhaleExpansionTintTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSubtreeWrapsIntoNextColumn();
    void testTintsByDepth();
    void testColorsCycle();
    void testGapBetweenColumns();
    void testFollowsScrolling();
    void testNoTintWithoutColors();
    void testNoTintInDetailsView();
    void testSettingsAreApplied();
    void testScreenshots();

private:
    int indexOf(const QString &relativePath) const;
    void expand(const QString &relativePath);
    QImage grabView() const;
    QColor pixel(const QImage &image, const QPointF &viewPos) const;
    QColor base() const;

    /**
     * @return A point inside the slot of the item at @p index, right of the item
     *         itself, i.e. on the background. Invalid if the item fills its slot.
     */
    QPointF backgroundPoint(int index) const;

    static QPalette lightPalette();
    static QPalette darkPalette();

    TestDir *m_testDir = nullptr;
    KFileItemModel *m_model = nullptr;
    DolphinItemListView *m_view = nullptr;
    KItemListContainer *m_container = nullptr;
    QPalette m_originalPalette;
};

void WhaleExpansionTintTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    m_originalPalette = QApplication::palette();
}

void WhaleExpansionTintTest::init()
{
    QApplication::setPalette(lightPalette());
    ExpansionTintSettings::defaults().save();

    m_testDir = new TestDir();
    m_testDir->createFiles({
        QStringLiteral("a/b/b1"),
        QStringLiteral("a/b/b2"),
        QStringLiteral("a/b/b3"),
        QStringLiteral("a/b/b4"),
        QStringLiteral("a/b/b5"),
        QStringLiteral("a/b/b6"),
        QStringLiteral("a/b/c/c1"),
        QStringLiteral("a/b/c/c2"),
        QStringLiteral("a/a1"),
        QStringLiteral("a/a2"),
        QStringLiteral("a/a-file-with-a-rather-long-name"),
        QStringLiteral("file0"),
        QStringLiteral("file-with-a-rather-long-name-at-the-root"),
    });

    CompactModeSettings::setExpandableFolders(true);
    CompactModeSettings::self()->save();
    m_model = new KFileItemModel();
    m_view = new DolphinItemListView();
    auto controller = new KItemListController(m_model, m_view, this);
    m_container = new KItemListContainer(controller);
#ifndef QT_NO_ACCESSIBILITY
    m_view->setAccessibleParentsObject(m_container);
#endif
    m_view->setItemLayout(KFileItemListView::CompactLayout);
    m_view->readSettings();
    QVERIFY(m_view->supportsItemExpanding());
    m_container->resize(720, 200);

    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    m_model->loadDirectory(m_testDir->url());
    QVERIFY(loaded.wait());

    m_container->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_container));

    expand(QStringLiteral("a"));
    expand(QStringLiteral("a/b"));
    expand(QStringLiteral("a/b/c"));
    QCOMPARE(m_model->count(), 16);
    QTest::qWait(300); // the animations of inserted items
}

void WhaleExpansionTintTest::cleanup()
{
    delete m_container; // with the controller, its model and view
    m_container = nullptr;
    m_view = nullptr;
    m_model = nullptr;
    delete m_testDir;
    m_testDir = nullptr;
    ExpansionTintSettings::defaults().save();
    QApplication::setPalette(m_originalPalette);
}

int WhaleExpansionTintTest::indexOf(const QString &relativePath) const
{
    return m_model->index(QUrl::fromLocalFile(m_testDir->path() + QLatin1Char('/') + relativePath));
}

void WhaleExpansionTintTest::expand(const QString &relativePath)
{
    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    QVERIFY(m_model->setExpanded(indexOf(relativePath), true));
    QVERIFY(loaded.wait());
}

QImage WhaleExpansionTintTest::grabView() const
{
    auto graphicsView = m_container->findChild<QGraphicsView *>();
    Q_ASSERT(graphicsView);
    return graphicsView->viewport()->grab().toImage();
}

QColor WhaleExpansionTintTest::pixel(const QImage &image, const QPointF &viewPos) const
{
    auto graphicsView = m_container->findChild<QGraphicsView *>();
    const QPoint pos = graphicsView->mapFromScene(m_view->mapToScene(viewPos)) * image.devicePixelRatio();
    return image.pixelColor(pos);
}

QColor WhaleExpansionTintTest::base() const
{
    return m_view->palette().color(QPalette::Base);
}

QPointF WhaleExpansionTintTest::backgroundPoint(int index) const
{
    const QRectF itemRect = m_view->itemRect(index);
    const QRectF slot = m_view->itemSlotRect(index);
    if (slot.right() - itemRect.right() < 4) {
        return QPointF(-1, -1);
    }
    return QPointF((itemRect.right() + slot.right()) / 2, itemRect.center().y());
}

QPalette WhaleExpansionTintTest::lightPalette()
{
    // Close to Breeze.
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0xef, 0xf0, 0xf1));
    palette.setColor(QPalette::WindowText, QColor(0x23, 0x26, 0x29));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf7, 0xf7));
    palette.setColor(QPalette::Text, QColor(0x23, 0x26, 0x29));
    palette.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    return palette;
}

QPalette WhaleExpansionTintTest::darkPalette()
{
    // Close to Breeze Dark.
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x20, 0x23, 0x26));
    palette.setColor(QPalette::WindowText, QColor(0xfc, 0xfc, 0xfc));
    palette.setColor(QPalette::Base, QColor(0x14, 0x16, 0x18));
    palette.setColor(QPalette::AlternateBase, QColor(0x1d, 0x1f, 0x22));
    palette.setColor(QPalette::Text, QColor(0xfc, 0xfc, 0xfc));
    palette.setColor(QPalette::Highlight, QColor(0x3d, 0xae, 0xe9));
    palette.setColor(QPalette::HighlightedText, QColor(0xfc, 0xfc, 0xfc));
    return palette;
}

void WhaleExpansionTintTest::testSubtreeWrapsIntoNextColumn()
{
    // The situation the tinting is made for: the children of a/b continue in the next column.
    const qreal bColumn = m_view->itemSlotRect(indexOf(QStringLiteral("a/b"))).left();
    const qreal lastChildColumn = m_view->itemSlotRect(indexOf(QStringLiteral("a/b/b6"))).left();
    QVERIFY(lastChildColumn > bColumn);
}

void WhaleExpansionTintTest::testTintsByDepth()
{
    const QColor depth1(0xff, 0xe0, 0xe0);
    const QColor depth2(0xe0, 0xff, 0xe0);
    const QColor depth3(0xe0, 0xe0, 0xff);
    m_view->setExpansionTintColors({depth1, depth2, depth3});
    QTest::qWait(50);
    const QImage image = grabView();

    int checked = 0;
    for (int index = 0; index < m_model->count(); ++index) {
        const QPointF pos = backgroundPoint(index);
        if (pos.x() < 0) {
            continue;
        }
        const int depth = m_model->expandedParentsCount(index);
        const QColor expected = depth == 0 ? base() : QList<QColor>{depth1, depth2, depth3}.at(depth - 1);
        QCOMPARE(pixel(image, pos), expected);
        ++checked;
    }
    QVERIFY(checked >= 10);

    // The tints lie below the items: the name of a tinted item is still drawn.
    const QRectF b1Rect = m_view->itemRect(indexOf(QStringLiteral("a/b/b1")));
    int textPixels = 0;
    for (qreal x = b1Rect.left(); x < b1Rect.right(); ++x) {
        if (pixel(image, QPointF(x, b1Rect.center().y())).lightness() < 128) {
            ++textPixels;
        }
    }
    QVERIFY(textPixels > 3);

    // Deeper items show the colors of the outer levels in their indentation.
    const int c1 = indexOf(QStringLiteral("a/b/c/c1"));
    const QRectF slot = m_view->itemSlotRect(c1);
    const qreal indentation = m_view->itemRect(c1).height();
    QCOMPARE(pixel(image, QPointF(slot.left() + indentation * 0.5, slot.center().y())), base());
    QCOMPARE(pixel(image, QPointF(slot.left() + indentation * 1.5, slot.top() + 1)), depth1);
    QCOMPARE(pixel(image, QPointF(slot.left() + indentation * 2.5, slot.top() + 1)), depth2);

    // The rows of a block touch each other: no gap between b1 and b2.
    const QRectF b1 = m_view->itemSlotRect(indexOf(QStringLiteral("a/b/b1")));
    const QRectF b2 = m_view->itemSlotRect(indexOf(QStringLiteral("a/b/b2")));
    QCOMPARE(b1.left(), b2.left());
    QCOMPARE(b1.bottom(), b2.top());
}

void WhaleExpansionTintTest::testColorsCycle()
{
    const QColor even(0xff, 0xe0, 0xe0);
    const QColor odd(0xe0, 0xff, 0xe0);
    m_view->setExpansionTintColors({even, odd});
    QTest::qWait(50);
    const QImage image = grabView();

    // Depth 3 starts the cycle again. Above the text of an item is background too.
    const int c1 = indexOf(QStringLiteral("a/b/c/c1"));
    QCOMPARE(m_model->expandedParentsCount(c1), 3);
    const QRectF slot = m_view->itemSlotRect(c1);
    QCOMPARE(pixel(image, QPointF(slot.right() - 1, slot.top() + 1)), even);
}

void WhaleExpansionTintTest::testGapBetweenColumns()
{
    m_view->setExpansionTintColors({QColor(0xff, 0xe0, 0xe0), QColor(0xe0, 0xff, 0xe0)});
    QTest::qWait(50);
    const QImage image = grabView();

    // Between the end of a column with tinted items and the start of the next one the base color stays.
    const int index = indexOf(QStringLiteral("a/b/b1"));
    const QRectF slot = m_view->itemSlotRect(index);
    const QPointF gap(slot.right() + 2, slot.center().y());
    QVERIFY(m_view->itemSlotRect(m_model->count() - 1).right() > gap.x());
    QCOMPARE(pixel(image, gap), base());
}

void WhaleExpansionTintTest::testFollowsScrolling()
{
    const QColor depth1(0xff, 0xe0, 0xe0);
    const QColor depth2(0xe0, 0xff, 0xe0);
    m_view->setExpansionTintColors({depth1, depth2});
    m_container->resize(260, 200);
    QTest::qWait(300);

    // Scroll so far that the column with b5 and b6 starts in the middle of the view.
    const int b6 = indexOf(QStringLiteral("a/b/b6"));
    const qreal columnStart = m_view->itemSlotRect(b6).left();
    QVERIFY(columnStart > 0);
    m_view->setScrollOffset(m_view->scrollOffset() + columnStart - 100);
    QTest::qWait(100);
    QCOMPARE(m_view->itemSlotRect(b6).left(), 100);

    const QImage image = grabView();
    const QRectF rect = m_view->itemRect(b6);
    QVERIFY(m_view->itemSlotRect(b6).right() > rect.right() + 4);
    QCOMPARE(pixel(image, QPointF(rect.right() + 3, rect.center().y())), depth2);
}

void WhaleExpansionTintTest::testNoTintWithoutColors()
{
    m_view->setExpansionTintColors({});
    QTest::qWait(50);
    const QImage image = grabView();

    for (int index = 0; index < m_model->count(); ++index) {
        const QPointF pos = backgroundPoint(index);
        if (pos.x() >= 0) {
            QCOMPARE(pixel(image, pos), base());
        }
    }
}

void WhaleExpansionTintTest::testNoTintInDetailsView()
{
    // The details view shows its tree differently; it is not tinted (yet).
    const QColor depth1(0xff, 0xe0, 0xe0);
    const QColor depth2(0xe0, 0xff, 0xe0);
    m_view->setExpansionTintColors({depth1, depth2});
    m_view->setItemLayout(KFileItemListView::DetailsLayout);
    m_view->readSettings();
    m_container->resize(720, 500); // all items visible
    QTest::qWait(300);
    QCOMPARE(m_view->itemLayout(), KFileItemListView::DetailsLayout);

    const QImage image = grabView();
    for (const QString &path : {QStringLiteral("a/a1"), QStringLiteral("a/b/b1")}) {
        const QRectF rect = m_view->itemRect(indexOf(path));
        QVERIFY(rect.bottom() < image.height());
        const QColor color = pixel(image, QPointF(rect.left() + rect.height() * 2.5, rect.top() + 1));
        QVERIFY(color != depth1 && color != depth2);
    }
}

void WhaleExpansionTintTest::testSettingsAreApplied()
{
    ExpansionTintSettings settings;
    settings.preset = ExpansionTintSettings::Preset::Custom;
    settings.customColors = {QColor(0x11, 0x22, 0x33), QColor(0x44, 0x55, 0x66)};
    settings.save();
    m_view->readSettings();
    QCOMPARE(m_view->expansionTintColors(), settings.customColors);

    settings.preset = ExpansionTintSettings::Preset::Off;
    settings.save();
    m_view->readSettings();
    QVERIFY(m_view->expansionTintColors().isEmpty());

    // Presets follow the palette.
    settings.preset = ExpansionTintSettings::Preset::Pastel;
    settings.save();
    m_view->readSettings();
    QCOMPARE(m_view->expansionTintColors(), ExpansionTintSettings::presetColors(settings.preset, lightPalette()));
    QApplication::setPalette(darkPalette());
    QTRY_COMPARE(m_view->expansionTintColors(), ExpansionTintSettings::presetColors(settings.preset, darkPalette()));
}

void WhaleExpansionTintTest::testScreenshots()
{
    const QString dir = qEnvironmentVariable("WHALE_TEST_SCREENSHOT_DIR");
    if (dir.isEmpty()) {
        QSKIP("WHALE_TEST_SCREENSHOT_DIR is not set");
    }

    using Preset = ExpansionTintSettings::Preset;
    const std::pair<QString, QPalette> themes[] = {{QStringLiteral("light"), lightPalette()}, {QStringLiteral("dark"), darkPalette()}};
    for (const auto &[theme, palette] : themes) {
        QApplication::setPalette(palette);
        for (const Preset preset : {Preset::Off, Preset::PrinterPaper, Preset::Grey, Preset::Pastel}) {
            ExpansionTintSettings settings = ExpansionTintSettings::defaults();
            settings.preset = preset;
            settings.save();
            m_view->readSettings();
            QTest::qWait(100);
            const QString name = QStringLiteral("tint-%1-%2.png").arg(theme, ExpansionTintSettings::presetName(preset).toLower());
            QVERIFY(m_container->grab().save(dir + QLatin1Char('/') + name));
        }
    }
}

QTEST_MAIN(WhaleExpansionTintTest)

#include "whaleexpansiontinttest.moc"
