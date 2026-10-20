/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The selection of a view limited to one folder, while the views of a split view are linked
 * (LK3, rules A1–A7 of .privatedoc/whale-split-lock-plan.md).
 */

#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "kitemviews/kitemlistselectionmanager.h"
#include "testdir.h"
#include "views/dolphinview.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

class WhaleSelectionLimitTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testUnlimited();
    void testCtrlToggleRefused();
    void testCtrlToggleInSameFolder();
    void testShiftArrowsClamped();
    void testRangeClamped();
    void testSelectAllInCurrentFolder();
    void testInvertInCurrentFolder();
    void testLimitingReducesSelection();
    void testLimitOff();

private:
    int indexOf(const QString &relativePath) const;
    KItemSet indexesOf(const QStringList &relativePaths) const;
    void setCurrent(const QString &relativePath);

    TestDir *m_testDir = nullptr;
    DolphinView *m_view = nullptr;
    KItemListContainer *m_container = nullptr;
    KFileItemModel *m_model = nullptr;
    KItemListSelectionManager *m_selectionManager = nullptr;
};

void WhaleSelectionLimitTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    qRegisterMetaType<KItemSet>("KItemSet");
}

void WhaleSelectionLimitTest::init()
{
    m_testDir = new TestDir();
    m_testDir->createFiles({
        QStringLiteral("a.txt"),
        QStringLiteral("b.txt"),
        QStringLiteral("dir/x.txt"),
        QStringLiteral("dir/y.txt"),
        QStringLiteral("dir/sub/z.txt"),
    });
    m_view = new DolphinView(m_testDir->url(), nullptr);
    m_view->setViewMode(DolphinView::DetailsView);
    m_view->resize(500, 400);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view));
    QTRY_COMPARE(m_view->itemsCount(), 3);
    m_container = m_view->findChild<KItemListContainer *>();
    QVERIFY(m_container);
    m_model = qobject_cast<KFileItemModel *>(m_container->controller()->model());
    QVERIFY(m_model);
    m_selectionManager = m_container->controller()->selectionManager();

    // dir, sub, x.txt, y.txt, a.txt, b.txt: the tree with "dir" expanded.
    QVERIFY(m_model->setExpanded(indexOf(QStringLiteral("dir")), true));
    QTRY_COMPARE(m_model->count(), 6);
    QCOMPARE(indexOf(QStringLiteral("dir")), 0);
    QCOMPARE(indexOf(QStringLiteral("dir/sub")), 1);
    QCOMPARE(indexOf(QStringLiteral("dir/x.txt")), 2);
    QCOMPARE(indexOf(QStringLiteral("dir/y.txt")), 3);
    QCOMPARE(indexOf(QStringLiteral("a.txt")), 4);
    m_container->setFocus();
}

void WhaleSelectionLimitTest::cleanup()
{
    delete m_view;
    m_view = nullptr;
    m_container = nullptr;
    m_model = nullptr;
    m_selectionManager = nullptr;
    delete m_testDir;
    m_testDir = nullptr;
}

int WhaleSelectionLimitTest::indexOf(const QString &relativePath) const
{
    return m_model->index(QUrl::fromLocalFile(m_testDir->path() + QLatin1Char('/') + relativePath));
}

KItemSet WhaleSelectionLimitTest::indexesOf(const QStringList &relativePaths) const
{
    KItemSet result;
    for (const QString &path : relativePaths) {
        result.insert(indexOf(path));
    }
    return result;
}

void WhaleSelectionLimitTest::setCurrent(const QString &relativePath)
{
    m_selectionManager->setCurrentItem(indexOf(relativePath));
    m_selectionManager->beginAnchoredSelection(indexOf(relativePath));
}

void WhaleSelectionLimitTest::testUnlimited()
{
    // Without the limit: as in Dolphin, across folders.
    QVERIFY(!m_view->isSelectionLimitedToOneFolder());
    m_view->selectAll();
    QCOMPARE(m_selectionManager->selectedItems().count(), 6);
    m_view->clearSelection();
    setCurrent(QStringLiteral("a.txt"));
    m_selectionManager->setSelected(indexOf(QStringLiteral("a.txt")));
    m_selectionManager->setSelected(indexOf(QStringLiteral("dir/x.txt")), 1, KItemListSelectionManager::Toggle);
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("a.txt"), QStringLiteral("dir/x.txt")}));
}

void WhaleSelectionLimitTest::testCtrlToggleRefused()
{
    // A2: Ctrl+click (Ctrl+Space here) on an item of another folder is refused.
    m_view->setSelectionLimitedToOneFolder(true);
    QSignalSpy limited(m_view, &DolphinView::selectionLimited);
    setCurrent(QStringLiteral("a.txt"));
    m_selectionManager->setSelected(indexOf(QStringLiteral("a.txt")));
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("dir/x.txt")));
    QTest::keyClick(m_container, Qt::Key_Space, Qt::ControlModifier);
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("a.txt")}));
    QCOMPARE(limited.count(), 1);

    // As the controller does it for a click: toggle, then anchor at the item.
    m_selectionManager->setSelected(indexOf(QStringLiteral("dir/y.txt")), 1, KItemListSelectionManager::Toggle);
    m_selectionManager->beginAnchoredSelection(indexOf(QStringLiteral("dir/y.txt")));
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("a.txt")}));
    QVERIFY(!m_selectionManager->isSelected(indexOf(QStringLiteral("dir/y.txt"))));
    QCOMPARE(limited.count(), 2);
}

void WhaleSelectionLimitTest::testCtrlToggleInSameFolder()
{
    m_view->setSelectionLimitedToOneFolder(true);
    QSignalSpy limited(m_view, &DolphinView::selectionLimited);
    setCurrent(QStringLiteral("dir/x.txt"));
    m_selectionManager->setSelected(indexOf(QStringLiteral("dir/x.txt")));
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("dir/sub")));
    QTest::keyClick(m_container, Qt::Key_Space, Qt::ControlModifier);
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/x.txt"), QStringLiteral("dir/sub")}));
    // Deselecting is always possible.
    m_selectionManager->setSelected(indexOf(QStringLiteral("dir/x.txt")), 1, KItemListSelectionManager::Toggle);
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/sub")}));
    QCOMPARE(limited.count(), 0);
}

void WhaleSelectionLimitTest::testShiftArrowsClamped()
{
    // A5: Shift+arrows do not select beyond the folder of the anchor.
    m_view->setSelectionLimitedToOneFolder(true);
    QSignalSpy limited(m_view, &DolphinView::selectionLimited);
    m_view->clearSelection();
    // As after a click on x.txt.
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("dir/x.txt")));
    m_selectionManager->setSelected(indexOf(QStringLiteral("dir/x.txt")));
    QTest::keyClick(m_container, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/x.txt"), QStringLiteral("dir/y.txt")}));
    QCOMPARE(limited.count(), 0);
    QTest::keyClick(m_container, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(m_selectionManager->currentItem(), indexOf(QStringLiteral("a.txt")));
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/x.txt"), QStringLiteral("dir/y.txt")}));
    QVERIFY(!m_selectionManager->isSelected(indexOf(QStringLiteral("a.txt"))));
    QCOMPARE(limited.count(), 1);
    // Up again: within the folder, as before.
    QTest::keyClick(m_container, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/x.txt"), QStringLiteral("dir/y.txt")}));
    QTest::keyClick(m_container, Qt::Key_Up, Qt::ShiftModifier);
    QTest::keyClick(m_container, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/sub"), QStringLiteral("dir/x.txt")}));
}

void WhaleSelectionLimitTest::testRangeClamped()
{
    // A3: a range (rubber band, Shift+click) keeps the items of the folder of the selection, else
    // of the current item.
    m_view->setSelectionLimitedToOneFolder(true);
    QSignalSpy limited(m_view, &DolphinView::selectionLimited);
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("dir/x.txt")));
    m_selectionManager->setSelectedItems(indexesOf({QStringLiteral("dir"), QStringLiteral("dir/sub"), QStringLiteral("dir/x.txt"), QStringLiteral("a.txt")}));
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/sub"), QStringLiteral("dir/x.txt")}));
    QCOMPARE(limited.count(), 1);

    // A growing rubber band: in the folder it started in.
    m_selectionManager->setSelectedItems(
        indexesOf({QStringLiteral("dir/sub"), QStringLiteral("dir/x.txt"), QStringLiteral("dir/y.txt"), QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/sub"), QStringLiteral("dir/x.txt"), QStringLiteral("dir/y.txt")}));
    QCOMPARE(limited.count(), 2);

    // Shift+click: the anchored range.
    m_view->clearSelection();
    setCurrent(QStringLiteral("dir/y.txt"));
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("b.txt")));
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/y.txt")}));
    QCOMPARE(limited.count(), 3);
    m_selectionManager->endAnchoredSelection();
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/y.txt")}));
}

void WhaleSelectionLimitTest::testSelectAllInCurrentFolder()
{
    // A4, A7: the folder of the current item, else the location.
    m_view->setSelectionLimitedToOneFolder(true);
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("dir/x.txt")));
    m_view->selectAll();
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/sub"), QStringLiteral("dir/x.txt"), QStringLiteral("dir/y.txt")}));

    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("b.txt")));
    m_view->selectAll();
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir"), QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

    m_view->clearSelection();
    m_selectionManager->setCurrentItem(-1);
    m_view->selectAll();
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir"), QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
}

void WhaleSelectionLimitTest::testInvertInCurrentFolder()
{
    // A6: within the folder of the current item.
    m_view->setSelectionLimitedToOneFolder(true);
    setCurrent(QStringLiteral("dir/x.txt"));
    m_selectionManager->setSelected(indexOf(QStringLiteral("dir/x.txt")));
    m_view->invertSelection();
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("dir/sub"), QStringLiteral("dir/y.txt")}));
}

void WhaleSelectionLimitTest::testLimitingReducesSelection()
{
    // A selection of several folders keeps the folder of the current item.
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("a.txt")));
    m_selectionManager->setSelectedItems(indexesOf({QStringLiteral("dir/x.txt"), QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QSignalSpy limited(m_view, &DolphinView::selectionLimited);
    m_view->setSelectionLimitedToOneFolder(true);
    QVERIFY(m_view->isSelectionLimitedToOneFolder());
    QCOMPARE(m_selectionManager->selectedItems(), indexesOf({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    QCOMPARE(limited.count(), 1);
}

void WhaleSelectionLimitTest::testLimitOff()
{
    m_view->setSelectionLimitedToOneFolder(true);
    m_view->setSelectionLimitedToOneFolder(false);
    QVERIFY(!m_view->isSelectionLimitedToOneFolder());
    m_selectionManager->setCurrentItem(indexOf(QStringLiteral("dir/x.txt")));
    m_view->selectAll();
    QCOMPARE(m_selectionManager->selectedItems().count(), 6);
}

QTEST_MAIN(WhaleSelectionLimitTest)

#include "whaleselectionlimittest.moc"
