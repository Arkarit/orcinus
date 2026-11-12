/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "settings/viewmodes/viewsettingstab.h"
#include "dolphin_compactmodesettings.h"
#include "dolphin_detailsmodesettings.h"
#include "dolphin_iconsmodesettings.h"

#include <QComboBox>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTest>

Q_DECLARE_METATYPE(ViewSettingsTab::Mode)

/**
 * Whale: the information density controls of the view mode settings (ID2).
 */
class ViewSettingsTabTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testDensityControls_data();
    void testDensityControls();
    void testApplyWritesTheDensity_data();
    void testApplyWritesTheDensity();
    void testDefaultsAreNormal();

private:
    struct Saved {
        int density;
        int padding;
        int lineHeight;
    };
    Saved m_icons{};
    Saved m_compact{};
    Saved m_details{};
};

template<typename Settings>
static void writeDensity(int density, int padding, int lineHeight)
{
    Settings::setDensity(density);
    Settings::setCustomPadding(padding);
    Settings::setCustomLineHeight(lineHeight);
    Settings::self()->save();
}

static void addModeRows()
{
    QTest::addColumn<ViewSettingsTab::Mode>("mode");
    QTest::newRow("icons") << ViewSettingsTab::IconsMode;
    QTest::newRow("compact") << ViewSettingsTab::CompactMode;
    QTest::newRow("details") << ViewSettingsTab::DetailsMode;
}

static QComboBox *densityBox(const ViewSettingsTab &tab)
{
    return tab.findChild<QComboBox *>(QStringLiteral("density"));
}

void ViewSettingsTabTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ViewSettingsTabTest::init()
{
    // The tests share their settings file.
    m_icons = {IconsModeSettings::density(), IconsModeSettings::customPadding(), IconsModeSettings::customLineHeight()};
    m_compact = {CompactModeSettings::density(), CompactModeSettings::customPadding(), CompactModeSettings::customLineHeight()};
    m_details = {DetailsModeSettings::density(), DetailsModeSettings::customPadding(), DetailsModeSettings::customLineHeight()};
    writeDensity<IconsModeSettings>(0, 2, 100);
    writeDensity<CompactModeSettings>(0, 2, 100);
    writeDensity<DetailsModeSettings>(0, 2, 100);
}

void ViewSettingsTabTest::cleanup()
{
    writeDensity<IconsModeSettings>(m_icons.density, m_icons.padding, m_icons.lineHeight);
    writeDensity<CompactModeSettings>(m_compact.density, m_compact.padding, m_compact.lineHeight);
    writeDensity<DetailsModeSettings>(m_details.density, m_details.padding, m_details.lineHeight);
}

void ViewSettingsTabTest::testDensityControls_data()
{
    addModeRows();
}

/**
 * Four levels; the custom values are shown only with "Custom", the line height not in the icons
 * view.
 */
void ViewSettingsTabTest::testDensityControls()
{
    QFETCH(ViewSettingsTab::Mode, mode);

    ViewSettingsTab tab(mode);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));

    QComboBox *box = densityBox(tab);
    QVERIFY(box);
    QCOMPARE(box->currentIndex(), 0);

    const auto spinBoxes = tab.findChildren<QSpinBox *>();
    QCOMPARE(spinBoxes.count(), mode == ViewSettingsTab::IconsMode ? 1 : 2);
    for (const QSpinBox *spinBox : spinBoxes) {
        QVERIFY(!spinBox->isVisible());
    }

    box->setCurrentIndex(3);
    for (const QSpinBox *spinBox : spinBoxes) {
        QVERIFY(spinBox->isVisible());
    }

    box->setCurrentIndex(1);
    for (const QSpinBox *spinBox : spinBoxes) {
        QVERIFY(!spinBox->isVisible());
    }
}

void ViewSettingsTabTest::testApplyWritesTheDensity_data()
{
    addModeRows();
}

void ViewSettingsTabTest::testApplyWritesTheDensity()
{
    QFETCH(ViewSettingsTab::Mode, mode);

    ViewSettingsTab tab(mode);
    densityBox(tab)->setCurrentIndex(3);
    for (QSpinBox *spinBox : tab.findChildren<QSpinBox *>()) {
        spinBox->setValue(spinBox->objectName() == QLatin1String("customLineHeight") ? 75 : 5);
    }
    tab.applySettings();

    switch (mode) {
    case ViewSettingsTab::IconsMode:
        QCOMPARE(IconsModeSettings::density(), 3);
        QCOMPARE(IconsModeSettings::customPadding(), 5);
        QCOMPARE(IconsModeSettings::customLineHeight(), 100);
        break;
    case ViewSettingsTab::CompactMode:
        QCOMPARE(CompactModeSettings::density(), 3);
        QCOMPARE(CompactModeSettings::customPadding(), 5);
        QCOMPARE(CompactModeSettings::customLineHeight(), 75);
        break;
    case ViewSettingsTab::DetailsMode:
        QCOMPARE(DetailsModeSettings::density(), 3);
        QCOMPARE(DetailsModeSettings::customPadding(), 5);
        QCOMPARE(DetailsModeSettings::customLineHeight(), 75);
        break;
    }

    // A new tab shows what was applied.
    ViewSettingsTab reopened(mode);
    QCOMPARE(densityBox(reopened)->currentIndex(), 3);
}

/**
 * "Defaults" means Dolphin's look, also for a new user who got Compact on the first start.
 */
void ViewSettingsTabTest::testDefaultsAreNormal()
{
    writeDensity<CompactModeSettings>(1, 2, 100);
    ViewSettingsTab tab(ViewSettingsTab::CompactMode);
    QCOMPARE(densityBox(tab)->currentIndex(), 1);
    tab.restoreDefaults();
    QCOMPARE(densityBox(tab)->currentIndex(), 0);
}

QTEST_MAIN(ViewSettingsTabTest)

#include "viewsettingstabtest.moc"
