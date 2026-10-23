/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "views/expansiontintsettings.h"

#include <KColorUtils>
#include <KConfigGroup>

#include <QPalette>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using Preset = ExpansionTintSettings::Preset;

class ExpansionTintSettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMissingGroupGivesDefaults();
    void testPresetColorCounts();
    void testPresetsFollowPalette();
    void testCustomColorsAreExact();
    void testRoundTrip();
    void testCustomColorsKeptForOtherPresets();
    void testColorCountLimits();
    void testInvalidEntries();
    void testPresetNames();

private:
    KSharedConfigPtr freshConfig() const;
    KSharedConfigPtr reopenedConfig() const;
    static QPalette palette(const QColor &base, const QColor &text);

    QTemporaryDir *m_dir = nullptr;
};

void ExpansionTintSettingsTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ExpansionTintSettingsTest::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
}

void ExpansionTintSettingsTest::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

KSharedConfigPtr ExpansionTintSettingsTest::freshConfig() const
{
    return KSharedConfig::openConfig(m_dir->filePath(QStringLiteral("whalerc")), KConfig::SimpleConfig);
}

KSharedConfigPtr ExpansionTintSettingsTest::reopenedConfig() const
{
    // A separate KConfig instance reads what has been written to disk.
    KSharedConfigPtr config = freshConfig();
    config->reparseConfiguration();
    return config;
}

QPalette ExpansionTintSettingsTest::palette(const QColor &base, const QColor &text)
{
    QPalette palette;
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::Text, text);
    return palette;
}

void ExpansionTintSettingsTest::testMissingGroupGivesDefaults()
{
    const ExpansionTintSettings settings = ExpansionTintSettings::load(freshConfig());
    QCOMPARE(settings.preset, Preset::Pastel);
    QCOMPARE(settings.customColors, ExpansionTintSettings::defaultCustomColors());
    QCOMPARE(settings, ExpansionTintSettings::defaults());
}

void ExpansionTintSettingsTest::testPresetColorCounts()
{
    const QPalette light = palette(Qt::white, Qt::black);
    QVERIFY(ExpansionTintSettings::presetColors(Preset::Off, light).isEmpty());
    QCOMPARE(ExpansionTintSettings::presetColors(Preset::PrinterPaper, light).count(), 2);
    QCOMPARE(ExpansionTintSettings::presetColors(Preset::Grey, light).count(), 2);
    QCOMPARE(ExpansionTintSettings::presetColors(Preset::Pastel, light).count(), 5);
    QCOMPARE(ExpansionTintSettings::presetColors(Preset::Custom, light), ExpansionTintSettings::defaultCustomColors());

    ExpansionTintSettings off;
    off.preset = Preset::Off;
    QVERIFY(off.colors(light).isEmpty());
}

void ExpansionTintSettingsTest::testPresetsFollowPalette()
{
    const QPalette light = palette(Qt::white, Qt::black);
    const QPalette dark = palette(QColor(0x23, 0x26, 0x29), QColor(0xfc, 0xfc, 0xfc));

    for (const Preset preset : {Preset::PrinterPaper, Preset::Grey, Preset::Pastel}) {
        const QList<QColor> lightColors = ExpansionTintSettings::presetColors(preset, light);
        const QList<QColor> darkColors = ExpansionTintSettings::presetColors(preset, dark);
        QCOMPARE(lightColors.count(), darkColors.count());

        QSet<QRgb> distinct;
        for (int i = 0; i < lightColors.count(); ++i) {
            // Subtle: close to the base color, but not equal to it.
            QVERIFY(lightColors.at(i) != Qt::white);
            QVERIFY(KColorUtils::luma(lightColors.at(i)) > 0.75);
            QVERIFY(KColorUtils::luma(darkColors.at(i)) < 0.1);
            distinct.insert(lightColors.at(i).rgb());
        }
        // Neighbouring depths can be told apart.
        QCOMPARE(distinct.count(), lightColors.count());
    }

    // The grey preset gets darker with depth on light schemes and lighter on dark ones.
    const QList<QColor> lightGrey = ExpansionTintSettings::presetColors(Preset::Grey, light);
    const QList<QColor> darkGrey = ExpansionTintSettings::presetColors(Preset::Grey, dark);
    QVERIFY(KColorUtils::luma(lightGrey.at(1)) < KColorUtils::luma(lightGrey.at(0)));
    QVERIFY(KColorUtils::luma(darkGrey.at(1)) > KColorUtils::luma(darkGrey.at(0)));
}

void ExpansionTintSettingsTest::testCustomColorsAreExact()
{
    ExpansionTintSettings settings;
    settings.preset = Preset::Custom;
    settings.customColors = {QColor(Qt::red), QColor(Qt::green), QColor(Qt::magenta)};

    QCOMPARE(settings.colors(palette(Qt::white, Qt::black)), settings.customColors);
    QCOMPARE(settings.colors(palette(Qt::black, Qt::white)), settings.customColors);
}

void ExpansionTintSettingsTest::testRoundTrip()
{
    ExpansionTintSettings settings;
    settings.preset = Preset::Custom;
    settings.customColors = {QColor(0x12, 0x34, 0x56), QColor(0xab, 0xcd, 0xef), QColor(0xfe, 0xdc, 0xba)};
    settings.save(freshConfig());

    QCOMPARE(ExpansionTintSettings::load(reopenedConfig()), settings);

    for (const Preset preset : {Preset::Off, Preset::PrinterPaper, Preset::Grey, Preset::Pastel}) {
        settings.preset = preset;
        settings.save(freshConfig());
        QCOMPARE(ExpansionTintSettings::load(reopenedConfig()).preset, preset);
    }
}

void ExpansionTintSettingsTest::testCustomColorsKeptForOtherPresets()
{
    ExpansionTintSettings settings;
    settings.preset = Preset::Grey;
    settings.customColors = {QColor(Qt::red), QColor(Qt::yellow)};
    settings.save(freshConfig());

    ExpansionTintSettings loaded = ExpansionTintSettings::load(reopenedConfig());
    QCOMPARE(loaded.preset, Preset::Grey);
    QCOMPARE(loaded.customColors, settings.customColors);

    loaded.preset = Preset::Custom;
    QCOMPARE(loaded.colors(QPalette()), settings.customColors);
}

void ExpansionTintSettingsTest::testColorCountLimits()
{
    // Fewer than two colors are not a cycle: the default colors are used instead.
    KConfigGroup group(freshConfig(), QStringLiteral("ExpansionTint"));
    group.writeEntry("Preset", QStringLiteral("Custom"));
    group.writeEntry("CustomColors", QStringList{QStringLiteral("#ff0000")});
    group.sync();
    QCOMPARE(ExpansionTintSettings::load(reopenedConfig()).customColors, ExpansionTintSettings::defaultCustomColors());

    // More than ten colors are cut off.
    QStringList names;
    for (int i = 0; i < 12; ++i) {
        names.append(QColor(i * 20, 0, 0).name());
    }
    group.writeEntry("CustomColors", names);
    group.sync();
    const QList<QColor> loaded = ExpansionTintSettings::load(reopenedConfig()).customColors;
    QCOMPARE(loaded.count(), ExpansionTintSettings::MaximumColorCount);
    QCOMPARE(loaded.first(), QColor(0, 0, 0));
    QCOMPARE(loaded.last(), QColor(180, 0, 0));

    // Exactly two colors are kept.
    group.writeEntry("CustomColors", QStringList{QStringLiteral("#ff0000"), QStringLiteral("#00ff00")});
    group.sync();
    QCOMPARE(ExpansionTintSettings::load(reopenedConfig()).customColors, (QList<QColor>{QColor(Qt::red), QColor(Qt::green)}));
}

void ExpansionTintSettingsTest::testInvalidEntries()
{
    KConfigGroup group(freshConfig(), QStringLiteral("ExpansionTint"));
    group.writeEntry("Preset", QStringLiteral("Unicorn"));
    group.writeEntry("CustomColors", QStringList{QStringLiteral("#ff0000"), QStringLiteral("nonsense"), QStringLiteral("#0000ff")});
    group.sync();

    const ExpansionTintSettings loaded = ExpansionTintSettings::load(reopenedConfig());
    // An unknown preset keeps the default, invalid colors are skipped.
    QCOMPARE(loaded.preset, Preset::Pastel);
    QCOMPARE(loaded.customColors, (QList<QColor>{QColor(Qt::red), QColor(Qt::blue)}));
}

void ExpansionTintSettingsTest::testPresetNames()
{
    for (const Preset preset : {Preset::Off, Preset::PrinterPaper, Preset::Grey, Preset::Pastel, Preset::Custom}) {
        const QString name = ExpansionTintSettings::presetName(preset);
        QVERIFY(!name.isEmpty());
        QCOMPARE(ExpansionTintSettings::presetFromName(name), std::optional<Preset>(preset));
    }
    QCOMPARE(ExpansionTintSettings::presetFromName(QString()), std::nullopt);
}

QTEST_GUILESS_MAIN(ExpansionTintSettingsTest)

#include "expansiontintsettingstest.moc"
