/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KITEMLISTSTYLEOPTION_H
#define KITEMLISTSTYLEOPTION_H

#include "dolphin_export.h"

#include <QFont>
#include <QFontMetrics>
#include <QPalette>
#include <QRect>

class DOLPHIN_EXPORT KItemListStyleOption
{
public:
    KItemListStyleOption();
    virtual ~KItemListStyleOption();

    QRect rect;
    QFont font;
    QFontMetrics fontMetrics;
    QPalette palette;
    int padding;
    int horizontalMargin;
    int verticalMargin;
    int iconSize;
    bool extendedSelectionRegion;
    int maxTextLines;
    int maxTextWidth;
    // Whale: information density (ID1). The space above and below the content of an item; -1 means
    // the same as padding, which stays the horizontal padding.
    int verticalPadding;
    // Whale: information density (ID1). The height of a text line of the Compact and Details
    // layouts in percent of the font's line height.
    int lineHeightPercent;

    /**
     * Whale: the space above and below the content of an item (ID1).
     */
    int effectiveVerticalPadding() const;

    /**
     * Whale: @p fontLineHeight scaled by lineHeightPercent, at least 1 (ID1).
     */
    int scaledLineHeight(int fontLineHeight) const;

    bool operator==(const KItemListStyleOption &other) const;
    bool operator!=(const KItemListStyleOption &other) const;
};
#endif
