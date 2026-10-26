/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphin_compactmodesettings.h"
#include "kitemviews/kfileitemlistview.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistselectionmanager.h"
#include "kitemviews/kstandarditemlistwidget.h"
#include "testdir.h"
#include "views/dolphinitemlistview.h"

#include <QDir>
#include <QFile>
#include <QGraphicsSceneMouseEvent>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

/**
 * Expandable folders in the compact view (CX1), and expanding and collapsing a whole branch
 * with Alt and a click on the expansion toggle or with * (CX2).
 *
 * WHALE_TEST_SCREENSHOT_DIR=<dir> saves screenshots of the compact view there.
 */
class WhaleCompactExpansionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSettingEnablesExpanding();
    void testCompactLayoutIndentation();
    void testArrowKeysMoveInCompactView();
    void testPlusAndMinusKeys();
    void testKeysAreTextDuringTypeAheadSearch();
    void testAsteriskExpandsBranch();
    void testAltClickExpandsAndCollapsesBranch();
    void testPlainClickExpandsOneLevel();
    void testRecursiveExpansionLimit();
    void testNoRecursionIntoLinks();

private:
    int indexOf(const QString &relativePath) const;
    KStandardItemListWidget *widgetFor(int index) const;
    void clickExpansionToggle(int index, Qt::KeyboardModifiers modifiers);
    void saveScreenshot(const QString &name);

    TestDir *m_testDir = nullptr;
    KFileItemModel *m_model = nullptr;
    DolphinItemListView *m_view = nullptr;
    KItemListContainer *m_container = nullptr;
    KItemListController *m_controller = nullptr;
    KItemListSelectionManager *m_selectionManager = nullptr;
};

void WhaleCompactExpansionTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    qRegisterMetaType<KItemSet>("KItemSet");
}

void WhaleCompactExpansionTest::init()
{
    m_testDir = new TestDir();
    m_testDir->createFiles({
        QStringLiteral("dir1/file1"),
        QStringLiteral("dir1/file2"),
        QStringLiteral("dir1/file3"),
        QStringLiteral("dir2/sub/deep/file4"),
        QStringLiteral("dir2/sub/file5"),
        QStringLiteral("dir2/file6"),
        QStringLiteral("file0"),
    });

    // As in Whale: the item size of the compact view comes from DolphinItemListView.
    CompactModeSettings::setExpandableFolders(true);
    CompactModeSettings::self()->save();
    m_model = new KFileItemModel();
    m_view = new DolphinItemListView();
    m_controller = new KItemListController(m_model, m_view, this);
    m_container = new KItemListContainer(m_controller);
#ifndef QT_NO_ACCESSIBILITY
    m_view->setAccessibleParentsObject(m_container);
#endif
    m_controller = m_container->controller();
    m_controller->setSelectionBehavior(KItemListController::MultiSelection);
    m_selectionManager = m_controller->selectionManager();
    m_view->setItemLayout(KFileItemListView::CompactLayout);
    m_view->readSettings();
    QVERIFY(m_view->supportsItemExpanding());
    m_container->resize(600, 160);

    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    m_model->loadDirectory(m_testDir->url());
    QVERIFY(loaded.wait());
    QCOMPARE(m_model->count(), 3);

    m_container->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_container));
    m_selectionManager->setCurrentItem(0);
}

void WhaleCompactExpansionTest::cleanup()
{
    delete m_container; // with the controller, its model and view
    m_container = nullptr;
    m_view = nullptr;
    m_controller = nullptr;
    m_selectionManager = nullptr;
    m_model = nullptr;
    delete m_testDir;
    m_testDir = nullptr;
}

int WhaleCompactExpansionTest::indexOf(const QString &relativePath) const
{
    return m_model->index(QUrl::fromLocalFile(m_testDir->path() + QLatin1Char('/') + relativePath));
}

KStandardItemListWidget *WhaleCompactExpansionTest::widgetFor(int index) const
{
    const auto items = m_view->childItems();
    for (QGraphicsItem *item : items) {
        auto widget = qobject_cast<KStandardItemListWidget *>(item->toGraphicsObject());
        if (widget && widget->isVisible() && widget->index() == index) {
            return widget;
        }
    }
    return nullptr;
}

void WhaleCompactExpansionTest::clickExpansionToggle(int index, Qt::KeyboardModifiers modifiers)
{
    KStandardItemListWidget *widget = widgetFor(index);
    QVERIFY(widget);
    const QRectF toggle = widget->expansionToggleRect();
    QVERIFY(!toggle.isEmpty());
    const QPointF pos = widget->mapToItem(m_view, toggle.center());
    QVERIFY(m_view->isAboveExpansionToggle(index, pos));

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setPos(pos);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    press.setModifiers(modifiers);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setPos(pos);
    release.setButton(Qt::LeftButton);
    release.setButtons(Qt::NoButton);
    release.setModifiers(modifiers);
    QCoreApplication::sendEvent(m_view, &press);
    QCoreApplication::sendEvent(m_view, &release);
}

void WhaleCompactExpansionTest::saveScreenshot(const QString &name)
{
    const QString dir = qEnvironmentVariable("WHALE_TEST_SCREENSHOT_DIR");
    if (!dir.isEmpty()) {
        QTest::qWait(300); // the animations of inserted items
        m_container->grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }
}

void WhaleCompactExpansionTest::testSettingEnablesExpanding()
{
    // The compact view supports expanding as the settings say; the icons view never does.
    const bool expandable = CompactModeSettings::expandableFolders();
    auto view = std::make_unique<DolphinItemListView>();
    CompactModeSettings::setExpandableFolders(true);
    CompactModeSettings::self()->save();
    view->setItemLayout(KFileItemListView::CompactLayout);
    view->readSettings();
    QVERIFY(view->supportsItemExpanding());

    CompactModeSettings::setExpandableFolders(false);
    CompactModeSettings::self()->save();
    view->readSettings();
    QVERIFY(!view->supportsItemExpanding());

    CompactModeSettings::setExpandableFolders(true);
    CompactModeSettings::self()->save();
    view->setItemLayout(KFileItemListView::IconsLayout);
    view->readSettings();
    QVERIFY(!view->supportsItemExpanding());

    CompactModeSettings::setExpandableFolders(expandable);
    CompactModeSettings::self()->save();
}

void WhaleCompactExpansionTest::testCompactLayoutIndentation()
{
    // The expansion toggle and the indentation come before the icon; the highlight does not
    // cover them.
    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    QVERIFY(m_model->setExpanded(indexOf(QStringLiteral("dir1")), true));
    QVERIFY(loaded.wait());
    QCOMPARE(m_model->count(), 6);
    QTest::qWait(300); // the animations of inserted items

    const int parent = indexOf(QStringLiteral("dir1"));
    const int child = indexOf(QStringLiteral("dir1/file1"));
    KStandardItemListWidget *parentWidget = widgetFor(parent);
    KStandardItemListWidget *childWidget = widgetFor(child);
    QVERIFY(parentWidget && childWidget);

    const QRectF parentToggle = parentWidget->expansionToggleRect();
    QVERIFY(!parentToggle.isEmpty());
    QVERIFY(childWidget->expansionToggleRect().isEmpty()); // a file
    // Icon and text after the toggle.
    QVERIFY(parentWidget->textRect().left() > parentToggle.right());
    QVERIFY(!parentWidget->selectionRectFull().intersects(parentToggle));
    // The child is indented by one level.
    const qreal parentTextX = parentWidget->textRect().left();
    const qreal childTextX = childWidget->textRect().left();
    QVERIFY(childTextX > parentTextX);
    // The text is not cut: the width includes the indentation.
    QVERIFY(childWidget->textRect().right() <= childWidget->size().width());
    saveScreenshot(QStringLiteral("compact-expanded"));
}

void WhaleCompactExpansionTest::testArrowKeysMoveInCompactView()
{
    // In the compact view, the arrow keys move between the items and columns, they do not
    // expand or collapse.
    QTest::keyClick(m_container, Qt::Key_Right);
    QVERIFY(!m_model->isExpanded(0));
    QVERIFY(m_model->count() == 3);

    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    QVERIFY(m_model->setExpanded(indexOf(QStringLiteral("dir1")), true));
    QVERIFY(loaded.wait());
    const int file3 = indexOf(QStringLiteral("dir1/file3"));
    m_selectionManager->setCurrentItem(file3);
    // Up: the previous item, not the parent folder.
    QTest::keyClick(m_container, Qt::Key_Up);
    QCOMPARE(m_selectionManager->currentItem(), file3 - 1);
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir1"))));
}

void WhaleCompactExpansionTest::testPlusAndMinusKeys()
{
    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("dir1")));
    QTest::keyClick(m_container, Qt::Key_Plus);
    QVERIFY(loaded.wait());
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir1"))));
    QCOMPARE(m_model->count(), 6);

    QTest::keyClick(m_container, Qt::Key_Minus);
    QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("dir1"))));
    QCOMPARE(m_model->count(), 3);

    // The selected folders too, as with the arrow keys in the details view.
    m_selectionManager->setSelected(indexOf(QStringLiteral("dir2")));
    QTest::keyClick(m_container, Qt::Key_Plus);
    QTRY_VERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir1"))) && m_model->isExpanded(indexOf(QStringLiteral("dir2"))));

    // A file: not handled, so it goes to the type-ahead search.
    m_selectionManager->clearSelection();
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("file0")));
    QTest::keyClick(m_container, Qt::Key_Minus);
    QVERIFY(m_controller->isSearchAsYouTypeActive());
}

void WhaleCompactExpansionTest::testKeysAreTextDuringTypeAheadSearch()
{
    // "dir-": the minus belongs to the search, it does not collapse the found folder.
    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    QVERIFY(m_model->setExpanded(indexOf(QStringLiteral("dir1")), true));
    QVERIFY(loaded.wait());
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("file0")));
    QTest::keyClicks(m_container, QStringLiteral("dir"));
    QVERIFY(m_controller->isSearchAsYouTypeActive());
    QCOMPARE(m_selectionManager->currentItem(), indexOf(QStringLiteral("dir1")));
    QTest::keyClick(m_container, Qt::Key_Minus);
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir1"))));
}

void WhaleCompactExpansionTest::testAsteriskExpandsBranch()
{
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("dir2")));
    QTest::keyClick(m_container, Qt::Key_Asterisk);
    QTRY_VERIFY(indexOf(QStringLiteral("dir2/sub/deep/file4")) >= 0);
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir2"))));
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir2/sub"))));
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir2/sub/deep"))));
    // Only the branch.
    QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("dir1"))));
    saveScreenshot(QStringLiteral("compact-branch"));
    // Lower: the branch continues at the top of the next column.
    m_container->resize(600, 100);
    saveScreenshot(QStringLiteral("compact-branch-wrapped"));
}

void WhaleCompactExpansionTest::testAltClickExpandsAndCollapsesBranch()
{
    QTest::qWait(300); // the layout of the first items
    clickExpansionToggle(indexOf(QStringLiteral("dir2")), Qt::AltModifier);
    QTRY_VERIFY(indexOf(QStringLiteral("dir2/sub/deep/file4")) >= 0);
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir2/sub/deep"))));
    QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("dir1"))));

    // Alt and a click collapse the branch and forget it: expanding shows one level only.
    QTest::qWait(300);
    clickExpansionToggle(indexOf(QStringLiteral("dir2")), Qt::AltModifier);
    QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("dir2"))));
    QCOMPARE(m_model->count(), 3);

    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    QVERIFY(m_model->setExpanded(indexOf(QStringLiteral("dir2")), true));
    QVERIFY(loaded.wait());
    QTest::qWait(100); // nothing more comes
    QVERIFY(indexOf(QStringLiteral("dir2/sub")) >= 0);
    QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("dir2/sub"))));
    QCOMPARE(indexOf(QStringLiteral("dir2/sub/file5")), -1);
}

void WhaleCompactExpansionTest::testPlainClickExpandsOneLevel()
{
    // Without Alt, as in Dolphin: one level, and the folders below come back as they were.
    QTest::qWait(300);
    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    clickExpansionToggle(indexOf(QStringLiteral("dir2")), Qt::NoModifier);
    QVERIFY(loaded.wait());
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir2"))));
    QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("dir2/sub"))));

    QVERIFY(m_model->setExpanded(indexOf(QStringLiteral("dir2/sub")), true));
    QVERIFY(loaded.wait());
    QTest::qWait(300);
    clickExpansionToggle(indexOf(QStringLiteral("dir2")), Qt::NoModifier);
    QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("dir2"))));
    QTest::qWait(300);
    clickExpansionToggle(indexOf(QStringLiteral("dir2")), Qt::NoModifier);
    QTRY_VERIFY(m_model->isExpanded(indexOf(QStringLiteral("dir2/sub"))));
}

void WhaleCompactExpansionTest::testRecursiveExpansionLimit()
{
    QStringList files;
    for (int i = 0; i < 20; ++i) {
        files.append(QStringLiteral("big/a%1/file").arg(i));
    }
    m_testDir->createFiles(files);
    QSignalSpy loaded(m_model, &KFileItemModel::directoryLoadingCompleted);
    m_model->refreshDirectory(m_testDir->url());
    QTRY_VERIFY(indexOf(QStringLiteral("big")) >= 0);

    // big has 20 folders, each with a file: the first level already adds more than 5 items.
    QSignalSpy limit(m_model, &KFileItemModel::recursiveExpansionLimitReached);
    m_model->setRecursiveExpansionLimit(5);
    QVERIFY(m_model->setExpandedRecursively(indexOf(QStringLiteral("big")), true));
    QTRY_COMPARE(limit.count(), 1);
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("big"))));
    QTest::qWait(200);
    for (int i = 0; i < 20; ++i) {
        QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("big/a%1").arg(i))));
    }
    QCOMPARE(m_model->recursiveExpansionLimit(), 5);
}

void WhaleCompactExpansionTest::testNoRecursionIntoLinks()
{
    // A link to a folder above: shown, but not expanded, so the expansion ends.
    m_testDir->createDir(QStringLiteral("loop/inner"));
    QVERIFY(QFile::link(m_testDir->path() + QStringLiteral("/loop"), m_testDir->path() + QStringLiteral("/loop/inner/back")));
    m_model->refreshDirectory(m_testDir->url());
    QTRY_VERIFY(indexOf(QStringLiteral("loop")) >= 0);

    QVERIFY(m_model->setExpandedRecursively(indexOf(QStringLiteral("loop")), true));
    QTRY_VERIFY(indexOf(QStringLiteral("loop/inner/back")) >= 0);
    QTest::qWait(300);
    QVERIFY(m_model->isExpanded(indexOf(QStringLiteral("loop/inner"))));
    QVERIFY(!m_model->isExpanded(indexOf(QStringLiteral("loop/inner/back"))));
}

QTEST_MAIN(WhaleCompactExpansionTest)

#include "whalecompactexpansiontest.moc"
