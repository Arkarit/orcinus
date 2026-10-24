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
#include "kitemviews/kitemlistheader.h"
#include "kitemviews/kitemlistwidget.h"
#include "testdir.h"
#include "views/dolphinitemlistview.h"
#include "views/expansiontintsettings.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGraphicsView>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
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
    void testSettingsPerView();
    void testTreeLinesOnlyInTheClassicLook();
    void testNamesInTheIndentation();
    void testNamesThatDoNotFitAreLeftOut();
    void testNameOpacity();
    void testNameSettingsAreApplied();
    void testNameScreenshots();
    void testDetailsTintsTheNameColumn();
    void testDetailsStripesRunThroughTheTints();
    void testDetailsNamesBelowTheirFolders();
    void testDetailsNamesStickToTheTop();
    void testDetailsScreenshots();
    void testScreenshots();

private:
    void setUpView(const QStringList &files, const QStringList &expanded);

    /**
     * Shows projects/settings/interface with twelve files, two more files in settings and
     * projects/tiny with one file, all expanded, for the tests of the names.
     */
    void setUpNamesView();

    /**
     * @return The number of dark pixels in @p rect, as text on the white base color gives.
     */
    int inkPixels(const QImage &image, const QRectF &rect) const;

    /**
     * @return The area of the indentation strip of @p level around the vertical center of the
     *         run of items from @p first to @p last.
     */
    QRectF stripAroundRun(const QString &first, const QString &last, int level) const;

    /**
     * Shows the view of setUpNamesView() as details view with name, size and date columns.
     */
    void setUpDetailsView();

    /**
     * @return The right end of the name column in the details view.
     */
    qreal nameColumnEnd() const;

    /**
     * @return The colors an alternate row can have in the light palette, with and without focus.
     */
    QList<QColor> alternateColors() const;
    void tearDownView();
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
    QTemporaryDir m_configDir;
};

void WhaleExpansionTintTest::initTestCase()
{
    // A configuration of its own: other tests running at the same time change settings in the
    // shared configuration of the test mode, like whether the compact view expands folders.
    // Data, cache and state go there too, instead of the user's directories.
    QVERIFY(m_configDir.isValid());
    for (const char *variable : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"}) {
        const QString path = m_configDir.filePath(QString::fromLatin1(variable).toLower());
        QVERIFY(QDir().mkpath(path));
        qputenv(variable, QFile::encodeName(path));
    }
    QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), m_configDir.filePath(QStringLiteral("xdg_config_home")));
    m_originalPalette = QApplication::palette();
}

void WhaleExpansionTintTest::init()
{
    QApplication::setPalette(lightPalette());
    // The tests of the tints check the backgrounds without the names in them.
    ExpansionTintSettings settings = ExpansionTintSettings::defaults();
    settings.showNames = false;
    settings.save(KSharedConfig::openConfig(), ExpansionTintSettings::View::Compact);
    settings.save(KSharedConfig::openConfig(), ExpansionTintSettings::View::Details);

    setUpView(
        {
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
        },
        {QStringLiteral("a"), QStringLiteral("a/b"), QStringLiteral("a/b/c")});
    QCOMPARE(m_model->count(), 16);
}

void WhaleExpansionTintTest::setUpView(const QStringList &files, const QStringList &expanded)
{
    m_testDir = new TestDir();
    m_testDir->createFiles(files);

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

    for (const QString &path : expanded) {
        expand(path);
    }
    QTest::qWait(300); // the animations of inserted items
}

void WhaleExpansionTintTest::tearDownView()
{
    delete m_container; // with the controller, its model and view
    m_container = nullptr;
    m_view = nullptr;
    m_model = nullptr;
    delete m_testDir;
    m_testDir = nullptr;
}

void WhaleExpansionTintTest::cleanup()
{
    tearDownView();
    ExpansionTintSettings::defaults().save(KSharedConfig::openConfig(), ExpansionTintSettings::View::Compact);
    ExpansionTintSettings::defaults().save(KSharedConfig::openConfig(), ExpansionTintSettings::View::Details);
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
        // An expanded folder (a, a/b, a/b/c) has the color of its children right of its arrow.
        const int level = m_model->expandedParentsCount(index) + (m_model->isExpanded(index) ? 1 : 0);
        const QColor expected = level == 0 ? base() : QList<QColor>{depth1, depth2, depth3}.at(level - 1);
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

    // The expansion arrow of an expanded folder keeps the color of its parent level; the
    // color of the children starts right after it, a few pixels before their own tint.
    for (const QString &path : {QStringLiteral("a"), QStringLiteral("a/b"), QStringLiteral("a/b/c")}) {
        const int index = indexOf(path);
        const int depth = m_model->expandedParentsCount(index);
        const QRectF folderSlot = m_view->itemSlotRect(index);
        const qreal height = m_view->itemRect(index).height();
        const QColor parentColor = depth == 0 ? base() : QList<QColor>{depth1, depth2, depth3}.at(depth - 1);
        const QColor childColor = QList<QColor>{depth1, depth2, depth3}.at(depth);
        QCOMPARE(pixel(image, QPointF(folderSlot.left() + depth * height + 1, folderSlot.top() + 1)), parentColor);
        QCOMPARE(pixel(image, QPointF(folderSlot.left() + (depth + 1) * height - 1, folderSlot.top() + 1)), childColor);
    }

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
    settings.preset = ExpansionTintSettings::Preset::PastelVivid;
    settings.save();
    m_view->readSettings();
    QCOMPARE(m_view->expansionTintColors(), ExpansionTintSettings::presetColors(settings.preset, lightPalette()));
    QApplication::setPalette(darkPalette());
    QTRY_COMPARE(m_view->expansionTintColors(), ExpansionTintSettings::presetColors(settings.preset, darkPalette()));
}

void WhaleExpansionTintTest::testSettingsPerView()
{
    using View = ExpansionTintSettings::View;
    ExpansionTintSettings compact = ExpansionTintSettings::defaults();
    compact.preset = ExpansionTintSettings::Preset::Custom;
    compact.customColors = {QColor(0x11, 0x22, 0x33), QColor(0x44, 0x55, 0x66)};
    compact.nameOpacity = 50;
    compact.save(KSharedConfig::openConfig(), View::Compact);
    ExpansionTintSettings details = ExpansionTintSettings::defaults();
    details.preset = ExpansionTintSettings::Preset::Custom;
    details.customColors = {QColor(0x77, 0x88, 0x99), QColor(0xaa, 0xbb, 0xcc), QColor(0xdd, 0xee, 0xff)};
    details.showNames = false;
    details.save(KSharedConfig::openConfig(), View::Details);

    m_view->readSettings();
    QCOMPARE(m_view->expansionTintColors(), compact.customColors);
    QCOMPARE(m_view->expansionNameOpacity(), 0.5);

    // Switching the view mode takes the settings of the new one.
    m_view->setItemLayout(KFileItemListView::DetailsLayout);
    QCOMPARE(m_view->expansionTintColors(), details.customColors);
    QCOMPARE(m_view->expansionNameOpacity(), 0.0);

    // The icons view has none.
    m_view->setItemLayout(KFileItemListView::IconsLayout);
    QVERIFY(m_view->expansionTintColors().isEmpty());
    QCOMPARE(m_view->expansionNameOpacity(), 0.0);

    m_view->setItemLayout(KFileItemListView::CompactLayout);
    QCOMPARE(m_view->expansionTintColors(), compact.customColors);
}

void WhaleExpansionTintTest::testTreeLinesOnlyInTheClassicLook()
{
    using Preset = ExpansionTintSettings::Preset;
    const auto widgetsShowTreeLines = [this](bool shown) {
        const auto items = m_view->childItems();
        int widgets = 0;
        for (QGraphicsItem *item : items) {
            if (auto widget = qobject_cast<KItemListWidget *>(item->toGraphicsObject()); widget && widget->isVisible()) {
                ++widgets;
                if (widget->treeLinesShown() != shown) {
                    return false;
                }
            }
        }
        return widgets > 0;
    };

    m_view->setItemLayout(KFileItemListView::DetailsLayout);
    for (const Preset preset : {Preset::Off, Preset::PastelVivid}) {
        ExpansionTintSettings settings = ExpansionTintSettings::defaults();
        settings.preset = preset;
        settings.save(KSharedConfig::openConfig(), ExpansionTintSettings::View::Details);
        m_view->readSettings();
        QVERIFY(!m_view->treeLinesShown());
        QTRY_VERIFY(widgetsShowTreeLines(false));
    }

    // Classic: no colors and no names, but the lines of the tree.
    ExpansionTintSettings classic = ExpansionTintSettings::defaults();
    classic.preset = Preset::Classic;
    classic.save(KSharedConfig::openConfig(), ExpansionTintSettings::View::Details);
    m_view->readSettings();
    QVERIFY(m_view->treeLinesShown());
    QTRY_VERIFY(widgetsShowTreeLines(true));
    QVERIFY(m_view->expansionTintColors().isEmpty());
    QCOMPARE(m_view->expansionNameOpacity(), 0.0);

    // The compact view has its own settings.
    m_view->setItemLayout(KFileItemListView::CompactLayout);
    QVERIFY(!m_view->treeLinesShown());
    QVERIFY(!m_view->expansionTintColors().isEmpty());
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
        for (const Preset preset : {Preset::Off, Preset::PrinterPaper, Preset::Grey, Preset::PastelSubtle, Preset::PastelVivid}) {
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

void WhaleExpansionTintTest::setUpNamesView()
{
    tearDownView();
    QStringList files;
    for (int i = 0; i < 12; ++i) {
        files.append(QStringLiteral("projects/settings/interface/settingspage%1.cpp").arg(i, 2, 10, QLatin1Char('0')));
    }
    files.append({QStringLiteral("projects/settings/general.cpp"), QStringLiteral("projects/settings/viewmodes.cpp"), QStringLiteral("projects/tiny/t.txt")});
    setUpView(
        files,
        {QStringLiteral("projects"), QStringLiteral("projects/settings"), QStringLiteral("projects/settings/interface"), QStringLiteral("projects/tiny")});
    QCOMPARE(m_model->count(), 19);
    m_view->setExpansionTintColors({});
    m_container->resize(720, 420);
    QTest::qWait(300);
}

int WhaleExpansionTintTest::inkPixels(const QImage &image, const QRectF &rect) const
{
    int count = 0;
    for (qreal y = rect.top(); y < rect.bottom(); ++y) {
        for (qreal x = rect.left(); x < rect.right(); ++x) {
            if (pixel(image, QPointF(x, y)).lightness() < 200) {
                ++count;
            }
        }
    }
    return count;
}

QRectF WhaleExpansionTintTest::stripAroundRun(const QString &first, const QString &last, int level) const
{
    const QRectF firstSlot = m_view->itemSlotRect(indexOf(first));
    const QRectF lastSlot = m_view->itemSlotRect(indexOf(last));
    const qreal indentation = m_view->itemRect(indexOf(first)).height();
    const qreal centerY = (firstSlot.top() + lastSlot.bottom()) / 2;
    const qreal reach = qMin(qreal(30), (lastSlot.bottom() - firstSlot.top()) / 2);
    return QRectF(firstSlot.left() + level * indentation + 1, centerY - reach, indentation - 2, 2 * reach);
}

void WhaleExpansionTintTest::testNamesInTheIndentation()
{
    setUpNamesView();
    m_view->setExpansionNameOpacity(1.0);
    QTest::qWait(50);
    const QImage image = grabView();

    // The contents of interface run down one column; next to them, below the arrows of
    // projects, settings and interface, their names.
    const QString first = QStringLiteral("projects/settings/interface/settingspage00.cpp");
    const QString last = QStringLiteral("projects/settings/interface/settingspage11.cpp");
    QCOMPARE(m_view->itemSlotRect(indexOf(first)).left(), m_view->itemSlotRect(indexOf(last)).left());
    for (int level = 0; level < 3; ++level) {
        QVERIFY2(inkPixels(image, stripAroundRun(first, last, level)) > 10, qPrintable(QString::number(level)));
    }

    // The files of settings after the contents of interface are a run of their own: their
    // parents are projects and settings, so the strip below the arrow of interface stays empty.
    const QString general = QStringLiteral("projects/settings/general.cpp");
    const QString viewmodes = QStringLiteral("projects/settings/viewmodes.cpp");
    QCOMPARE(m_view->itemSlotRect(indexOf(general)).left(), m_view->itemSlotRect(indexOf(viewmodes)).left());
    QCOMPARE(inkPixels(image, stripAroundRun(general, viewmodes, 2)), 0);
}

void WhaleExpansionTintTest::testNamesThatDoNotFitAreLeftOut()
{
    setUpNamesView();
    m_view->setExpansionNameOpacity(1.0);
    QTest::qWait(50);
    const QImage image = grabView();

    // The only file of tiny is a run of one row: neither "projects" nor "tiny" fits next to it.
    const QString file = QStringLiteral("projects/tiny/t.txt");
    QCOMPARE(m_model->expandedParentsCount(indexOf(file)), 2);
    for (int level = 0; level < 2; ++level) {
        QCOMPARE(inkPixels(image, stripAroundRun(file, file, level)), 0);
    }

    // Hidden altogether.
    m_view->setExpansionNameOpacity(0);
    QTest::qWait(50);
    const QImage hidden = grabView();
    const QString first = QStringLiteral("projects/settings/interface/settingspage00.cpp");
    const QString last = QStringLiteral("projects/settings/interface/settingspage11.cpp");
    for (int level = 0; level < 3; ++level) {
        QCOMPARE(inkPixels(hidden, stripAroundRun(first, last, level)), 0);
    }
}

void WhaleExpansionTintTest::testNameOpacity()
{
    setUpNamesView();
    const QString first = QStringLiteral("projects/settings/interface/settingspage00.cpp");
    const QString last = QStringLiteral("projects/settings/interface/settingspage11.cpp");
    const auto darkest = [&]() {
        QTest::qWait(50);
        const QImage image = grabView();
        const QRectF strip = stripAroundRun(first, last, 1);
        int lightness = 255;
        for (qreal y = strip.top(); y < strip.bottom(); ++y) {
            for (qreal x = strip.left(); x < strip.right(); ++x) {
                lightness = qMin(lightness, pixel(image, QPointF(x, y)).lightness());
            }
        }
        return lightness;
    };

    m_view->setExpansionNameOpacity(1.0);
    const int opaque = darkest();
    m_view->setExpansionNameOpacity(0.3);
    const int faint = darkest();
    QVERIFY2(faint > opaque + 50, qPrintable(QStringLiteral("%1 %2").arg(faint).arg(opaque)));
    QVERIFY(faint < 255);
}

void WhaleExpansionTintTest::testNameSettingsAreApplied()
{
    ExpansionTintSettings settings = ExpansionTintSettings::defaults();
    QVERIFY(settings.showNames);
    settings.nameOpacity = 60;
    settings.save();
    m_view->readSettings();
    QCOMPARE(m_view->expansionNameOpacity(), 0.6);

    settings.showNames = false;
    settings.save();
    m_view->readSettings();
    QCOMPARE(m_view->expansionNameOpacity(), 0.0);
}

void WhaleExpansionTintTest::setUpDetailsView()
{
    setUpNamesView();
    m_view->setItemLayout(KFileItemListView::DetailsLayout);
    m_view->setVisibleRoles({"text", "size", "modificationtime"});
    m_container->resize(720, 460);
    QTest::qWait(300);
}

qreal WhaleExpansionTintTest::nameColumnEnd() const
{
    return m_view->header()->leftPadding() + m_view->header()->columnWidth("text");
}

QList<QColor> WhaleExpansionTintTest::alternateColors() const
{
    const QPalette palette = m_view->palette();
    return {KItemListWidget::alternateBackgroundColor(palette, true), KItemListWidget::alternateBackgroundColor(palette, false)};
}

void WhaleExpansionTintTest::testDetailsTintsTheNameColumn()
{
    setUpDetailsView();
    const QColor depth1(0xff, 0xe0, 0xe0);
    const QColor depth2(0xe0, 0xff, 0xe0);
    const QColor depth3(0xe0, 0xe0, 0xff);
    m_view->setExpansionTintColors({depth1, depth2, depth3});
    QTest::qWait(50);
    const QImage image = grabView();

    // An even row (no stripe): the tint of its depth up to the end of the name column, not beyond.
    const int index = indexOf(QStringLiteral("projects/settings/interface/settingspage01.cpp"));
    QCOMPARE(index % 2, 0);
    QCOMPARE(m_model->expandedParentsCount(index), 3);
    const QRectF row = m_view->itemRect(index);
    QCOMPARE(pixel(image, QPointF(row.left() + nameColumnEnd() - 3, row.top() + 1)), depth3);
    QCOMPARE(pixel(image, QPointF(row.left() + nameColumnEnd() + 3, row.top() + 1)), base());

    // The outer levels show in the indentation, the expanded folder carries the color of its children.
    const qreal indentation = row.height();
    const qreal start = m_view->highlightEntireRow() ? m_view->header()->leftPadding() : 0;
    QCOMPARE(pixel(image, QPointF(row.left() + start + 1.5 * indentation, row.top() + 1)), depth1);
    QCOMPARE(pixel(image, QPointF(row.left() + start + 2.5 * indentation, row.top() + 1)), depth2);
    const int interface = indexOf(QStringLiteral("projects/settings/interface"));
    const QRectF heading = m_view->itemRect(interface);
    QCOMPARE(pixel(image, QPointF(heading.left() + nameColumnEnd() - 3, heading.top() + 1)), depth3);
}

void WhaleExpansionTintTest::testDetailsStripesRunThroughTheTints()
{
    setUpDetailsView();
    const QList<QColor> alternates = alternateColors();

    const int odd = indexOf(QStringLiteral("projects/settings/interface/settingspage02.cpp"));
    QCOMPARE(odd % 2, 1);
    const QRectF row = m_view->itemRect(odd);
    const QPointF inName(row.left() + nameColumnEnd() - 3, row.top() + 1);
    const QPointF inSize(row.left() + nameColumnEnd() + 3, row.top() + 1);

    // Without tints the widgets paint the stripes.
    m_view->setExpansionTintColors({});
    QTest::qWait(50);
    QImage image = grabView();
    QVERIFY(alternates.contains(pixel(image, inSize)));
    const QColor stripe = pixel(image, inSize);
    QCOMPARE(pixel(image, inName), stripe);

    // With tints the stripe goes on beyond the name column, and within it the tint is shifted by
    // what the stripe differs from the base color.
    const QColor tint(0xe0, 0xe0, 0xff);
    m_view->setExpansionTintColors({QColor(0xff, 0xe0, 0xe0), QColor(0xe0, 0xff, 0xe0), tint});
    QTest::qWait(50);
    image = grabView();
    QCOMPARE(pixel(image, inSize), stripe);
    const QColor shifted(tint.red() + stripe.red() - 255, tint.green() + stripe.green() - 255, tint.blue() + stripe.blue() - 255);
    QCOMPARE(pixel(image, inName), shifted);
}

void WhaleExpansionTintTest::testDetailsNamesBelowTheirFolders()
{
    setUpDetailsView();
    m_view->setExpansionTintColors({});
    m_view->setExpansionNameOpacity(1.0);
    QTest::qWait(50);
    const QImage image = grabView();

    const QRectF heading = m_view->itemRect(indexOf(QStringLiteral("projects/settings/interface")));
    const qreal indentation = heading.height();
    const qreal start = m_view->highlightEntireRow() ? m_view->header()->leftPadding() : 0;
    const auto strip = [&](int level, qreal top, qreal height) {
        return QRectF(heading.left() + start + level * indentation + 1, top, indentation - 2, height);
    };

    // The name of interface starts right below its row, in the strip below its arrow.
    QVERIFY(inkPixels(image, strip(2, heading.bottom() + 1, 2 * indentation)) > 10);

    // The only file of tiny leaves no room for its name.
    const QRectF file = m_view->itemRect(indexOf(QStringLiteral("projects/tiny/t.txt")));
    QCOMPARE(inkPixels(image, strip(1, file.top(), file.height())), 0);

    // Hidden altogether.
    m_view->setExpansionNameOpacity(0);
    QTest::qWait(50);
    QCOMPARE(inkPixels(grabView(), strip(2, heading.bottom() + 1, 2 * indentation)), 0);
}

void WhaleExpansionTintTest::testDetailsNamesStickToTheTop()
{
    setUpDetailsView();
    m_view->setExpansionTintColors({});
    m_view->setExpansionNameOpacity(1.0);
    m_container->resize(720, 240);
    QTest::qWait(100);

    // Scrolled so far that projects, settings and interface are above the view.
    const QString firstShown = QStringLiteral("projects/settings/interface/settingspage03.cpp");
    m_view->setScrollOffset(m_view->itemRect(indexOf(firstShown)).top() - m_view->itemRect(0).top());
    QTest::qWait(100);
    QVERIFY(m_view->itemRect(indexOf(QStringLiteral("projects/settings/interface"))).bottom() <= m_view->itemRect(indexOf(firstShown)).top());
    const QImage image = grabView();

    const QRectF top = m_view->itemRect(indexOf(firstShown));
    const qreal indentation = top.height();
    const qreal start = m_view->highlightEntireRow() ? m_view->header()->leftPadding() : 0;
    for (int level = 0; level < 3; ++level) {
        const QRectF strip(top.left() + start + level * indentation + 1, top.top(), indentation - 2, 2 * indentation);
        QVERIFY2(inkPixels(image, strip) > 10, qPrintable(QString::number(level)));
    }
}

void WhaleExpansionTintTest::testNameScreenshots()
{
    const QString dir = qEnvironmentVariable("WHALE_TEST_SCREENSHOT_DIR");
    if (dir.isEmpty()) {
        QSKIP("WHALE_TEST_SCREENSHOT_DIR is not set");
    }

    setUpNamesView();
    using Preset = ExpansionTintSettings::Preset;
    const std::pair<QString, QPalette> themes[] = {{QStringLiteral("light"), lightPalette()}, {QStringLiteral("dark"), darkPalette()}};
    for (const auto &[theme, palette] : themes) {
        QApplication::setPalette(palette);
        for (const Preset preset : {Preset::PastelVivid, Preset::Off}) {
            ExpansionTintSettings settings = ExpansionTintSettings::defaults();
            settings.preset = preset;
            settings.save();
            m_view->readSettings();
            QTest::qWait(100);
            const QString name = QStringLiteral("names-%1-%2.png").arg(theme, ExpansionTintSettings::presetName(preset).toLower());
            QVERIFY(m_container->grab().save(dir + QLatin1Char('/') + name));
        }
    }
}

void WhaleExpansionTintTest::testDetailsScreenshots()
{
    const QString dir = qEnvironmentVariable("WHALE_TEST_SCREENSHOT_DIR");
    if (dir.isEmpty()) {
        QSKIP("WHALE_TEST_SCREENSHOT_DIR is not set");
    }

    setUpNamesView();
    m_view->setItemLayout(KFileItemListView::DetailsLayout);
    m_view->setVisibleRoles({"text", "size", "modificationtime"});
    m_container->resize(720, 460);
    using Preset = ExpansionTintSettings::Preset;
    const std::pair<QString, QPalette> themes[] = {{QStringLiteral("light"), lightPalette()}, {QStringLiteral("dark"), darkPalette()}};
    for (const auto &[theme, palette] : themes) {
        QApplication::setPalette(palette);
        for (const Preset preset : {Preset::PastelVivid, Preset::PastelSubtle, Preset::Off}) {
            ExpansionTintSettings settings = ExpansionTintSettings::defaults();
            settings.preset = preset;
            settings.save(KSharedConfig::openConfig(), ExpansionTintSettings::View::Details);
            m_view->readSettings();
            QTest::qWait(150);
            const QString name = QStringLiteral("details-%1-%2.png").arg(theme, ExpansionTintSettings::presetName(preset).toLower());
            QVERIFY(m_container->grab().save(dir + QLatin1Char('/') + name));
        }
    }

    // Scrolled so that the expanded folders are above the view: their names stick to the top.
    QApplication::setPalette(lightPalette());
    ExpansionTintSettings::defaults().save(KSharedConfig::openConfig(), ExpansionTintSettings::View::Details);
    m_view->readSettings();
    m_container->resize(720, 240);
    QTest::qWait(100);
    m_view->setScrollOffset(m_view->itemRect(indexOf(QStringLiteral("projects/settings/interface/settingspage03.cpp"))).top() - m_view->itemRect(0).top());
    QTest::qWait(150);
    QVERIFY(m_container->grab().save(dir + QStringLiteral("/details-light-scrolled.png")));
}

QTEST_MAIN(WhaleExpansionTintTest)

#include "whaleexpansiontinttest.moc"
