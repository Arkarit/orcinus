/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef EXPANSIONTINTSETTINGS_H
#define EXPANSIONTINTSETTINGS_H

#include "dolphin_export.h"

#include <KSharedConfig>

#include <QColor>
#include <QList>

#include <optional>

class QPalette;

/**
 * @brief Global settings for tinting the background of expanded folders.
 *
 * Items inside an expanded folder get a background color that depends on
 * their depth. The colors form a cycle: depth d uses colors[(d - 1) % n].
 *
 * The presets are mixed into the base color of the palette, so that they suit
 * light and dark color schemes. Custom colors are used exactly as chosen.
 *
 * Independently of the colors, the names of the expanded folders can be shown
 * in the indentation of their contents.
 *
 * The compact and the details view have settings of their own, stored in the groups
 * [ExpansionTint] and [ExpansionTintDetails] of the application configuration.
 */
struct DOLPHIN_EXPORT ExpansionTintSettings {
    enum class View {
        Compact,
        Details,
    };

    enum class Preset {
        Off,
        /**
         * Only for the details view: no colors and no names, but the lines of the tree,
         * as the details view showed expanded folders before.
         */
        Classic,
        PrinterPaper,
        Grey,
        PastelSubtle,
        PastelVivid,
        Custom,
    };

    static constexpr int MinimumColorCount = 2;
    static constexpr int MaximumColorCount = 10;

    Preset preset = Preset::PastelVivid;

    /**
     * The colors of the preset Custom. They are kept while another preset is
     * selected. Always contains between MinimumColorCount and MaximumColorCount
     * valid colors after loading.
     */
    QList<QColor> customColors;

    static constexpr int MinimumNameOpacity = 10;
    static constexpr int MaximumNameOpacity = 100;

    /**
     * Whether the names of the expanded folders are shown in the indentation of their contents.
     */
    bool showNames = true;

    /**
     * Opacity of these names in percent, between MinimumNameOpacity and MaximumNameOpacity.
     */
    int nameOpacity = 40;

    /**
     * @return The settings used if no configuration exists: the pastel preset.
     */
    static ExpansionTintSettings defaults();

    /**
     * @return The custom colors offered if none have been chosen yet.
     */
    static QList<QColor> defaultCustomColors();

    /**
     * @return The colors of @p preset for @p palette. Empty for Off; for Custom
     *         the default custom colors.
     */
    static QList<QColor> presetColors(Preset preset, const QPalette &palette);

    /**
     * @return True if the names of the expanded folders are shown: they are switched on and
     *         the preset is not Classic.
     */
    bool namesShown() const;

    /**
     * @return The effective color cycle for @p palette. Empty if tinting is off.
     */
    QList<QColor> colors(const QPalette &palette) const;

    static ExpansionTintSettings load(const KSharedConfigPtr &config = KSharedConfig::openConfig(), View view = View::Compact);
    void save(const KSharedConfigPtr &config = KSharedConfig::openConfig(), View view = View::Compact) const;

    static QString presetName(Preset preset);
    static std::optional<Preset> presetFromName(const QString &name);

    bool operator==(const ExpansionTintSettings &other) const;
};

#endif
