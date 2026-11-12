/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VIEWMODESETTINGS_H
#define VIEWMODESETTINGS_H

#include "kitemviews/kstandarditemlistview.h"
#include "views/dolphinview.h"
#include "viewsettingstab.h"

#include <variant>

class CompactModeSettings;
class DetailsModeSettings;
class IconsModeSettings;

/**
 * @short Helper class for accessing similar properties of IconsModeSettings,
 *        CompactModeSettings and DetailsModeSettings.
 */
class ViewModeSettings
{
public:
    /**
     * Whale: information density of a view mode (ID1). The values are the choices of the
     * kcfg entry Density.
     */
    enum class Density {
        Normal,
        Compact,
        VeryCompact,
        Custom,
    };

    explicit ViewModeSettings(DolphinView::Mode mode);
    explicit ViewModeSettings(ViewSettingsTab::Mode mode);
    explicit ViewModeSettings(KStandardItemListView::ItemLayout itemLayout);

    void setIconSize(int iconSize);
    int iconSize() const;

    void setPreviewSize(int previewSize);
    int previewSize() const;

    void setUseSystemFont(bool useSystemFont);
    bool useSystemFont() const;

    void setViewFont(const QFont &font);
    QFont viewFont() const;

    // Whale: information density (ID1)
    void setDensity(Density density);
    Density density() const;
    void setCustomPadding(int padding);
    int customPadding() const;
    void setCustomLineHeight(int percent);
    int customLineHeight() const;

    /**
     * Whale: the space above and below each item for @p density (ID1): Normal 2, Compact 1,
     * VeryCompact 0, Custom customPadding().
     */
    int verticalPadding() const;

    /**
     * Whale: the height of a text line in percent of the font's line height (ID1): 100 except
     * with the custom density.
     */
    int lineHeightPercent() const;

    void useDefaults(bool useDefaults);

    void readConfig();
    void save();

private:
    explicit ViewModeSettings();

    std::variant<IconsModeSettings *, CompactModeSettings *, DetailsModeSettings *> m_viewModeSettingsVariant;
};

#endif
