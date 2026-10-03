/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kitemviews/kfileitemlistview.h"
#include "kitemviews/kfileitemlistwidget.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistcontroller.h"
#include "testdir.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace
{
// Customizes the font of all items while enabled, like a per-item style rule would.
bool s_boldItems = false;

class BoldInformant : public KFileItemListWidgetInformant
{
protected:
    QFont itemFont(int index, const KItemListView *view, const QFont &baseFont) const override
    {
        QFont font = KFileItemListWidgetInformant::itemFont(index, view, baseFont);
        font.setBold(s_boldItems);
        return font;
    }
};

class BoldWidget : public KFileItemListWidget
{
public:
    BoldWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent)
        : KFileItemListWidget(informant, parent)
    {
    }

    static KItemListWidgetInformant *createInformant()
    {
        return new BoldInformant();
    }

protected:
    QFont customizedFont(const QFont &baseFont) const override
    {
        QFont font = KFileItemListWidget::customizedFont(baseFont);
        font.setBold(s_boldItems);
        return font;
    }
};

class TestListView : public KFileItemListView
{
public:
    using KItemListView::invalidateItemAppearance;
    using KItemListView::setStyleOption;
    using KItemListView::visibleItemListWidgets;
};
}

class KStandardItemListWidgetTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testNameTextColorOnlyAffectsName();
    void testNameTextColorOfHiddenItem();
    void testNameTextColorOfSelectedItem();
    void testTextColorStillTintsAdditionalInformation();
    void testNameTextColorFollowsPalette();
    void testItemFontIsUsedForSizeHints();
    void testInvalidateItemAppearance();

private:
    KStandardItemListWidget *widgetForName(const QString &name) const;
    void loadDirectory(const QStringList &files);

    TestDir *m_testDir = nullptr;
    KFileItemModel *m_model = nullptr;
    TestListView *m_view = nullptr;
    KItemListContainer *m_container = nullptr;
};

void KStandardItemListWidgetTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void KStandardItemListWidgetTest::init()
{
    s_boldItems = false;

    m_testDir = new TestDir();
    m_model = new KFileItemModel();
    m_model->setShowHiddenFiles(true);
    m_view = new TestListView();
    m_view->setWidgetCreator(new KItemListWidgetCreator<BoldWidget>());
    m_view->setItemLayout(KStandardItemListView::DetailsLayout);
    m_view->setVisibleRoles({"text", "size"});
    // The container owns the controller, which owns the model and the view.
    auto controller = new KItemListController(m_model, m_view, nullptr);
    m_container = new KItemListContainer(controller);
    m_container->resize(800, 600);
    m_container->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_container));
}

void KStandardItemListWidgetTest::cleanup()
{
    delete m_container;
    m_container = nullptr;
    m_view = nullptr;
    m_model = nullptr;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    delete m_testDir;
    m_testDir = nullptr;
}

void KStandardItemListWidgetTest::loadDirectory(const QStringList &files)
{
    m_testDir->createFiles(files);
    QSignalSpy loadingCompleted(m_model, &KFileItemModel::directoryLoadingCompleted);
    m_model->loadDirectory(m_testDir->url());
    QVERIFY(loadingCompleted.wait());
    QCOMPARE(m_model->count(), files.count());
    QCoreApplication::processEvents();
}

KStandardItemListWidget *KStandardItemListWidgetTest::widgetForName(const QString &name) const
{
    const auto widgets = m_view->visibleItemListWidgets();
    for (KItemListWidget *widget : widgets) {
        if (widget->data().value("text").toString() == name) {
            auto standardWidget = qobject_cast<KStandardItemListWidget *>(widget);
            standardWidget->triggerCacheRefreshing();
            return standardWidget;
        }
    }
    return nullptr;
}

void KStandardItemListWidgetTest::testNameTextColorOnlyAffectsName()
{
    loadDirectory({QStringLiteral("a")});
    KStandardItemListWidget *widget = widgetForName(QStringLiteral("a"));
    QVERIFY(widget);

    const QColor defaultName = widget->textColor(*m_container);
    const QColor defaultAdditionalInfo = widget->m_additionalInfoTextColor;
    QVERIFY(!widget->nameTextColor().isValid());

    widget->setNameTextColor(Qt::red);
    QCOMPARE(widget->textColor(*m_container), QColor(Qt::red));
    QCOMPARE(widget->m_additionalInfoTextColor, defaultAdditionalInfo);

    widget->setNameTextColor(QColor());
    QCOMPARE(widget->textColor(*m_container), defaultName);
    QCOMPARE(widget->m_additionalInfoTextColor, defaultAdditionalInfo);
}

void KStandardItemListWidgetTest::testNameTextColorOfHiddenItem()
{
    loadDirectory({QStringLiteral(".hidden")});
    KStandardItemListWidget *widget = widgetForName(QStringLiteral(".hidden"));
    QVERIFY(widget);
    QVERIFY(widget->m_isHidden);

    // Without a name color, hidden items use the faded additional information color.
    QCOMPARE(widget->textColor(*m_container), widget->m_additionalInfoTextColor);
    const QColor defaultAdditionalInfo = widget->m_additionalInfoTextColor;

    widget->setNameTextColor(Qt::red);
    QCOMPARE(widget->textColor(*m_container), widget->fadedTextColor(Qt::red));
    QVERIFY(widget->textColor(*m_container) != QColor(Qt::red));
    QCOMPARE(widget->m_additionalInfoTextColor, defaultAdditionalInfo);
}

void KStandardItemListWidgetTest::testNameTextColorOfSelectedItem()
{
    loadDirectory({QStringLiteral("a")});
    KStandardItemListWidget *widget = widgetForName(QStringLiteral("a"));
    QVERIFY(widget);

    // Selected items use the style's color for the selection state (Text or HighlightedText,
    // depending on the style) and ignore the name color.
    widget->setSelected(true);
    const QColor selectedName = widget->textColor(*m_container);
    widget->setNameTextColor(Qt::red);
    QCOMPARE(widget->textColor(*m_container), selectedName);
    QVERIFY(widget->textColor(*m_container) != QColor(Qt::red));
}

void KStandardItemListWidgetTest::testTextColorStillTintsAdditionalInformation()
{
    loadDirectory({QStringLiteral("a")});
    KStandardItemListWidget *widget = widgetForName(QStringLiteral("a"));
    QVERIFY(widget);

    const QColor defaultAdditionalInfo = widget->m_additionalInfoTextColor;
    widget->setTextColor(Qt::red);
    QCOMPARE(widget->textColor(*m_container), QColor(Qt::red));
    QVERIFY(widget->m_additionalInfoTextColor != defaultAdditionalInfo);
}

void KStandardItemListWidgetTest::testNameTextColorFollowsPalette()
{
    loadDirectory({QStringLiteral(".hidden")});
    KStandardItemListWidget *widget = widgetForName(QStringLiteral(".hidden"));
    QVERIFY(widget);

    widget->setNameTextColor(Qt::red);
    const QColor fadedOnDefaultBase = widget->textColor(*m_container);

    KItemListStyleOption option = m_view->styleOption();
    option.palette.setColor(QPalette::Base, option.palette.base().color().lightness() > 128 ? Qt::black : Qt::white);
    m_view->setStyleOption(option);

    QCOMPARE(widget->textColor(*m_container), widget->fadedTextColor(Qt::red));
    QVERIFY(widget->textColor(*m_container) != fadedOnDefaultBase);
}

void KStandardItemListWidgetTest::testItemFontIsUsedForSizeHints()
{
    loadDirectory({QStringLiteral("a rather long file name.txt")});
    const BoldInformant informant;

    auto sizeHints = [&](KStandardItemListView::ItemLayout layout) {
        m_view->setItemLayout(layout);
        QVector<std::pair<qreal, bool>> hints(1, {0.0, false});
        qreal width = 0.0;
        informant.calculateItemSizeHints(hints, width, m_view);
        return std::pair{hints.first().first, width};
    };

    s_boldItems = false;
    const qreal normalColumnWidth = informant.preferredRoleColumnWidth("text", 0, m_view);
    const qreal normalCompactWidth = sizeHints(KStandardItemListView::CompactLayout).first;

    s_boldItems = true;
    QVERIFY(informant.preferredRoleColumnWidth("text", 0, m_view) > normalColumnWidth);
    QVERIFY(sizeHints(KStandardItemListView::CompactLayout).first > normalCompactWidth);
}

void KStandardItemListWidgetTest::testInvalidateItemAppearance()
{
    loadDirectory({QStringLiteral("a")});
    KStandardItemListWidget *widget = widgetForName(QStringLiteral("a"));
    QVERIFY(widget);
    QVERIFY(!widget->m_customizedFont.bold());

    s_boldItems = true;
    m_view->invalidateItemAppearance();
    widget = widgetForName(QStringLiteral("a"));
    QVERIFY(widget);
    QVERIFY(widget->m_customizedFont.bold());
}

QTEST_MAIN(KStandardItemListWidgetTest)

#include "kstandarditemlistwidgettest.moc"
