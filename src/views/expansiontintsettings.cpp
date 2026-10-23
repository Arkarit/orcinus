/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "expansiontintsettings.h"

#include <KColorUtils>
#include <KConfigGroup>

#include <QPalette>

namespace
{
const QString groupName = QStringLiteral("ExpansionTint");

// A preset color: a hue mixed into the base color by the given amount.
// An invalid hue stands for the text color of the palette, which gives grey tones.
struct Tint {
    QColor hue;
    qreal amount;
};

// Provisional values, to be decided after a visual review with real light and dark color schemes.
// No blue: it would be mistaken for the selection or hover color.
const Tint pastelTints[] = {
    {QColor(0xf5, 0xc4, 0x00), 0.10}, // yellow
    {QColor(0xe8, 0x50, 0x8a), 0.10}, // rose
    {QColor(0x2e, 0xcc, 0x8f), 0.10}, // mint
    {QColor(0xff, 0x8c, 0x42), 0.10}, // peach
    {QColor(0x9b, 0x6b, 0xdf), 0.10}, // lavender
};

const Tint printerPaperTints[] = {
    {QColor(), 0.05}, // light grey
    {QColor(0x2e, 0xcc, 0x8f), 0.10}, // light mint
};

const Tint greyTints[] = {
    {QColor(), 0.04},
    {QColor(), 0.08},
};

template<std::size_t N>
QList<QColor> mixedColors(const Tint (&tints)[N], const QColor &base, const QColor &text)
{
    QList<QColor> colors;
    for (const Tint &tint : tints) {
        // Rounded to 8 bits per channel, as the colors are stored, so that they compare equal after saving.
        colors.append(QColor(KColorUtils::mix(base, tint.hue.isValid() ? tint.hue : text, tint.amount).rgb()));
    }
    return colors;
}

QList<QColor> presetColorsFor(ExpansionTintSettings::Preset preset, const QColor &base, const QColor &text)
{
    switch (preset) {
    case ExpansionTintSettings::Preset::Off:
        return {};
    case ExpansionTintSettings::Preset::PrinterPaper:
        return mixedColors(printerPaperTints, base, text);
    case ExpansionTintSettings::Preset::Grey:
        return mixedColors(greyTints, base, text);
    case ExpansionTintSettings::Preset::Pastel:
        return mixedColors(pastelTints, base, text);
    case ExpansionTintSettings::Preset::Custom:
        return ExpansionTintSettings::defaultCustomColors();
    }
    return {};
}
}

ExpansionTintSettings ExpansionTintSettings::defaults()
{
    ExpansionTintSettings settings;
    settings.preset = Preset::Pastel;
    settings.customColors = defaultCustomColors();
    return settings;
}

QList<QColor> ExpansionTintSettings::defaultCustomColors()
{
    // The pastel preset as it looks on white.
    return mixedColors(pastelTints, Qt::white, Qt::black);
}

QList<QColor> ExpansionTintSettings::presetColors(Preset preset, const QPalette &palette)
{
    return presetColorsFor(preset, palette.color(QPalette::Base), palette.color(QPalette::Text));
}

QList<QColor> ExpansionTintSettings::colors(const QPalette &palette) const
{
    if (preset == Preset::Custom) {
        return customColors;
    }
    return presetColors(preset, palette);
}

ExpansionTintSettings ExpansionTintSettings::load(const KSharedConfigPtr &config)
{
    ExpansionTintSettings settings = defaults();
    if (!config->hasGroup(groupName)) {
        return settings;
    }

    const KConfigGroup group(config, groupName);

    // An unknown preset, e.g. written by a newer version, keeps the default.
    const std::optional<Preset> preset = presetFromName(group.readEntry("Preset", QString()));
    if (preset.has_value()) {
        settings.preset = *preset;
    }

    QList<QColor> customColors;
    const QStringList colorNames = group.readEntry("CustomColors", QStringList());
    for (const QString &name : colorNames) {
        const QColor color = QColor::fromString(name);
        if (color.isValid() && customColors.count() < MaximumColorCount) {
            customColors.append(color);
        }
    }
    if (customColors.count() >= MinimumColorCount) {
        settings.customColors = customColors;
    }

    return settings;
}

void ExpansionTintSettings::save(const KSharedConfigPtr &config) const
{
    KConfigGroup group(config, groupName);
    group.writeEntry("Preset", presetName(preset));

    QStringList colorNames;
    for (const QColor &color : customColors) {
        colorNames.append(color.name(QColor::HexRgb));
    }
    group.writeEntry("CustomColors", colorNames);

    config->sync();
}

QString ExpansionTintSettings::presetName(Preset preset)
{
    switch (preset) {
    case Preset::Off:
        return QStringLiteral("Off");
    case Preset::PrinterPaper:
        return QStringLiteral("PrinterPaper");
    case Preset::Grey:
        return QStringLiteral("Grey");
    case Preset::Pastel:
        return QStringLiteral("Pastel");
    case Preset::Custom:
        return QStringLiteral("Custom");
    }
    return QString();
}

std::optional<ExpansionTintSettings::Preset> ExpansionTintSettings::presetFromName(const QString &name)
{
    for (const auto preset : {Preset::Off, Preset::PrinterPaper, Preset::Grey, Preset::Pastel, Preset::Custom}) {
        if (presetName(preset) == name) {
            return preset;
        }
    }
    return std::nullopt;
}

bool ExpansionTintSettings::operator==(const ExpansionTintSettings &other) const
{
    return preset == other.preset && customColors == other.customColors;
}
