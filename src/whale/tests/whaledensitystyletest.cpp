/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaledensitystyle.h"
#include "filterbar/filterbar.h"
#include "settings/interface/folderstabssettingspage.h"
#include "settings/interface/statusandlocationbarssettingspage.h"
#include "statusbar/dolphinstatusbar.h"
#include "whaletoolbarsettings.h"

#include <KConfigGroup>
#include <KStyleExtensions>

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPixmap>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

using Density = WhaleToolbarSettings::Density;

/**
 * Whale: the toolbar density (TD1).
 */
class WhaleDensityStyleTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testNormalKeepsTheApplicationStyle();
    void testOnlyToolBarsGetLower();
    void testFieldsKeepTheirHeightWhenCompact();
    void testToolBarsGetLowerStepByStep();
    void testTheSettingIsFollowed();
    void testANewApplicationStyleIsWrappedAgain();
    void testCustomElementsOfTheStyleStayAvailable();
    void testFilterBarAndStatusBarFollowTheDensity();
    void testSettingsPage();

private:
    struct Bar {
        std::unique_ptr<QWidget> window;
        QToolBar *toolBar = nullptr;
        QToolButton *buttonInBar = nullptr;
        QLineEdit *fieldInBar = nullptr;
        QToolButton *buttonOutside = nullptr;
        QLineEdit *fieldOutside = nullptr;
    };
    /** A window with a toolbar (a button, a field) and the same widgets outside of it. */
    static Bar createBar();

    QString m_styleName;
    bool m_hadDensity = false;
    Density m_density = Density::Normal;
};

void WhaleDensityStyleTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    // A known base style: the metrics below do not depend on the desktop.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    m_styleName = QApplication::style()->name();
}

void WhaleDensityStyleTest::init()
{
    m_hadDensity = WhaleToolbarSettings::self()->hasToolbarDensity();
    m_density = WhaleToolbarSettings::self()->toolbarDensity();
}

void WhaleDensityStyleTest::cleanup()
{
    // The tests share their settings file.
    QSignalBlocker blocker(WhaleToolbarSettings::self());
    if (m_hadDensity) {
        WhaleToolbarSettings::self()->setToolbarDensity(m_density);
    } else {
        KConfigGroup group = WhaleToolbarSettings::self()->config()->group(WhaleToolbarSettings::groupName());
        group.deleteEntry("ToolbarDensity");
        group.sync();
    }
    WhaleDensityStyle::apply(Density::Normal);
}

// An icon that exists without an icon theme (offscreen).
static QIcon icon()
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::darkBlue);
    return QIcon(pixmap);
}

WhaleDensityStyleTest::Bar WhaleDensityStyleTest::createBar()
{
    Bar bar;
    bar.window = std::make_unique<QWidget>();
    auto layout = new QVBoxLayout(bar.window.get());
    bar.toolBar = new QToolBar(bar.window.get());
    bar.toolBar->setIconSize(QSize(22, 22));
    bar.buttonInBar = new QToolButton(bar.toolBar);
    bar.buttonInBar->setIcon(icon());
    bar.buttonInBar->setIconSize(QSize(22, 22));
    bar.toolBar->addWidget(bar.buttonInBar);
    bar.fieldInBar = new QLineEdit(bar.toolBar);
    bar.toolBar->addWidget(bar.fieldInBar);
    bar.buttonOutside = new QToolButton(bar.window.get());
    bar.buttonOutside->setIcon(icon());
    bar.buttonOutside->setIconSize(QSize(22, 22));
    bar.fieldOutside = new QLineEdit(bar.window.get());
    layout->addWidget(bar.toolBar);
    layout->addWidget(bar.buttonOutside);
    layout->addWidget(bar.fieldOutside);
    return bar;
}

void WhaleDensityStyleTest::testNormalKeepsTheApplicationStyle()
{
    WhaleDensityStyle::apply(Density::Compact);
    QVERIFY(dynamic_cast<WhaleDensityStyle *>(QApplication::style()));

    WhaleDensityStyle::apply(Density::Normal);
    QVERIFY(!dynamic_cast<WhaleDensityStyle *>(QApplication::style()));
    QCOMPARE(QApplication::style()->name(), m_styleName);
}

/**
 * Buttons inside a toolbar get lower; the same buttons elsewhere (dialogs, views) do not.
 */
void WhaleDensityStyleTest::testOnlyToolBarsGetLower()
{
    const Bar normal = createBar();
    // Size hints follow the current application style: remember them before changing it.
    const int normalButton = normal.buttonInBar->sizeHint().height();
    const int normalFieldOutside = normal.fieldOutside->sizeHint().height();
    QCOMPARE(normal.buttonOutside->sizeHint().height(), normalButton);

    WhaleDensityStyle::apply(Density::VeryCompact);
    const Bar bar = createBar();
    QVERIFY(bar.buttonInBar->sizeHint().height() < normalButton);
    QCOMPARE(bar.buttonOutside->sizeHint().height(), normalButton);
    QCOMPARE(bar.fieldOutside->sizeHint().height(), normalFieldOutside);

    QVERIFY(WhaleDensityStyle::isInBar(bar.fieldInBar));
    QVERIFY(!WhaleDensityStyle::isInBar(bar.fieldOutside));
    QCOMPARE(QApplication::style()->pixelMetric(QStyle::PM_ToolBarItemMargin, nullptr, bar.toolBar), 1);
    QVERIFY(QApplication::style()->pixelMetric(QStyle::PM_ToolBarItemMargin, nullptr, bar.window.get()) >= 1);
}

/**
 * Compact keeps the height of text fields, so that the style still draws their frames; Very
 * compact lowers them as well.
 */
void WhaleDensityStyleTest::testFieldsKeepTheirHeightWhenCompact()
{
    const Bar normal = createBar();
    // Size hints follow the current application style: remember them before changing it.
    const int normalField = normal.fieldInBar->sizeHint().height();
    const int normalButton = normal.buttonInBar->sizeHint().height();

    WhaleDensityStyle::apply(Density::Compact);
    const Bar compact = createBar();
    QCOMPARE(compact.fieldInBar->sizeHint().height(), normalField);
    QVERIFY(compact.buttonInBar->sizeHint().height() < normalButton);

    WhaleDensityStyle::apply(Density::VeryCompact);
    const Bar veryCompact = createBar();
    QVERIFY(veryCompact.fieldInBar->sizeHint().height() < normalField);
    // Never lower than the text.
    QVERIFY(veryCompact.fieldInBar->sizeHint().height() >= veryCompact.fieldInBar->fontMetrics().height());
}

void WhaleDensityStyleTest::testToolBarsGetLowerStepByStep()
{
    int previous = createBar().toolBar->sizeHint().height();
    for (const Density density : {Density::Compact, Density::VeryCompact}) {
        WhaleDensityStyle::apply(density);
        const int height = createBar().toolBar->sizeHint().height();
        QVERIFY2(height < previous, qPrintable(QStringLiteral("%1 < %2").arg(height).arg(previous)));
        previous = height;
    }
}

void WhaleDensityStyleTest::testTheSettingIsFollowed()
{
    WhaleToolbarSettings::self()->setToolbarDensity(Density::Normal);
    WhaleDensityStyle::follow();
    QVERIFY(!dynamic_cast<WhaleDensityStyle *>(QApplication::style()));

    WhaleToolbarSettings::self()->setToolbarDensity(Density::Compact);
    auto style = dynamic_cast<WhaleDensityStyle *>(QApplication::style());
    QVERIFY(style);
    QCOMPARE(style->density(), Density::Compact);

    WhaleToolbarSettings::self()->setToolbarDensity(Density::VeryCompact);
    style = dynamic_cast<WhaleDensityStyle *>(QApplication::style());
    QVERIFY(style);
    QCOMPARE(style->density(), Density::VeryCompact);

    WhaleToolbarSettings::self()->setToolbarDensity(Density::Normal);
    QVERIFY(!dynamic_cast<WhaleDensityStyle *>(QApplication::style()));
    QCOMPARE(QApplication::style()->name(), m_styleName);
}

/**
 * Another application style (e.g. changed in Plasma's settings) replaces the proxy; it is
 * wrapped again.
 */
void WhaleDensityStyleTest::testANewApplicationStyleIsWrappedAgain()
{
    WhaleDensityStyle::follow();
    WhaleToolbarSettings::self()->setToolbarDensity(Density::Compact);
    QVERIFY(dynamic_cast<WhaleDensityStyle *>(QApplication::style()));

    // A widget must exist: the style change reaches the application through widgets.
    const Bar bar = createBar();
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Windows")));
    QTRY_VERIFY(dynamic_cast<WhaleDensityStyle *>(QApplication::style()));
    QCOMPARE(dynamic_cast<WhaleDensityStyle *>(QApplication::style())->baseStyle()->name(), QStringLiteral("windows"));

    // Back to the base style of the other tests.
    WhaleToolbarSettings::self()->setToolbarDensity(Density::Normal);
    QApplication::setStyle(QStyleFactory::create(m_styleName));
}

/**
 * KDE widgets ask the style for custom elements (KCapacityBar in the status bar: without its
 * CE_CapacityBar it paints an old gradient); the proxy must not hide them.
 */
void WhaleDensityStyleTest::testCustomElementsOfTheStyleStayAvailable()
{
    QStyle *breeze = QStyleFactory::create(QStringLiteral("Breeze"));
    if (!breeze) {
        QSKIP("Breeze is not installed");
    }
    QApplication::setStyle(breeze);
    QWidget widget;
    const QStyle::ControlElement withoutProxy = KStyleExtensions::customControlElement(QStringLiteral("CE_CapacityBar"), &widget);
    QVERIFY(withoutProxy != 0);

    WhaleDensityStyle::apply(Density::Compact);
    QVERIFY(dynamic_cast<WhaleDensityStyle *>(QApplication::style()));
    QCOMPARE(KStyleExtensions::customControlElement(QStringLiteral("CE_CapacityBar"), &widget), withoutProxy);
    QCOMPARE(QApplication::style()->objectName(), QStringLiteral("breeze"));

    WhaleDensityStyle::apply(Density::Normal);
    QApplication::setStyle(QStyleFactory::create(m_styleName));
}

/**
 * The filter bar and the status bar get lower too, also while they are shown (TD3).
 */
void WhaleDensityStyleTest::testFilterBarAndStatusBarFollowTheDensity()
{
    QWidget window;
    auto layout = new QVBoxLayout(&window);
    auto filterBar = new FilterBar(&window);
    auto statusBar = new DolphinStatusBar(&window);
    layout->addWidget(filterBar);
    layout->addWidget(statusBar);
    window.resize(800, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    filterBar->setVisible(true, WithoutAnimation);
    statusBar->setVisible(true, WithoutAnimation);
    QTRY_VERIFY(filterBar->height() > 0 && statusBar->height() > 0);
    const int normalFilterBar = filterBar->maximumHeight();
    const int normalStatusBar = statusBar->maximumHeight();

    int previousFilterBar = normalFilterBar;
    int previousStatusBar = normalStatusBar;
    for (const Density density : {Density::Compact, Density::VeryCompact}) {
        WhaleDensityStyle::apply(density);
        QTRY_VERIFY(statusBar->maximumHeight() < previousStatusBar);
        QTRY_VERIFY(filterBar->maximumHeight() <= previousFilterBar);
        previousFilterBar = filterBar->maximumHeight();
        previousStatusBar = statusBar->maximumHeight();
    }
    QVERIFY(filterBar->maximumHeight() < normalFilterBar);

    // Back to Dolphin's heights.
    WhaleDensityStyle::apply(Density::Normal);
    QTRY_COMPARE(statusBar->maximumHeight(), normalStatusBar);
    QTRY_COMPARE(filterBar->maximumHeight(), normalFilterBar);
}

/**
 * Interface → Status & Location bars → Bar density (TD4).
 */
void WhaleDensityStyleTest::testSettingsPage()
{
    WhaleToolbarSettings::self()->setToolbarDensity(Density::Compact);
    WhaleDensityStyle::follow();

    FoldersTabsSettingsPage foldersPage(nullptr);
    StatusAndLocationBarsSettingsPage page(nullptr, &foldersPage);
    auto box = page.findChild<QComboBox *>(QStringLiteral("barDensity"));
    QVERIFY(box);
    QCOMPARE(box->count(), 3);
    QCOMPARE(box->currentIndex(), static_cast<int>(Density::Compact));

    box->setCurrentIndex(static_cast<int>(Density::VeryCompact));
    page.applySettings();
    QCOMPARE(WhaleToolbarSettings::self()->toolbarDensity(), Density::VeryCompact);
    auto style = dynamic_cast<WhaleDensityStyle *>(QApplication::style());
    QVERIFY(style);
    QCOMPARE(style->density(), Density::VeryCompact);

    // "Defaults" means Dolphin's look.
    page.restoreDefaults();
    QCOMPARE(box->currentIndex(), static_cast<int>(Density::Normal));

    // A profile applied while the dialog is open (TD5): the page shows it, OK keeps it.
    WhaleToolbarSettings::self()->setToolbarDensity(Density::Compact);
    QCOMPARE(box->currentIndex(), static_cast<int>(Density::Compact));

    WhaleToolbarSettings::self()->setToolbarDensity(Density::Normal);
    QVERIFY(!dynamic_cast<WhaleDensityStyle *>(QApplication::style()));
}

QTEST_MAIN(WhaleDensityStyleTest)

#include "whaledensitystyletest.moc"
