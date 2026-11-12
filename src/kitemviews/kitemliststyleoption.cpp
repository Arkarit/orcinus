/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kitemliststyleoption.h"

KItemListStyleOption::KItemListStyleOption()
    : rect()
    , font()
    , fontMetrics(QFont())
    , palette()
    , padding(-1)
    , horizontalMargin(-1)
    , verticalMargin(-1)
    , iconSize(-1)
    , extendedSelectionRegion(false)
    , maxTextLines(0)
    , maxTextWidth(0)
    , verticalPadding(-1)
    , lineHeightPercent(100)
{
}

KItemListStyleOption::~KItemListStyleOption() = default;

bool KItemListStyleOption::operator==(const KItemListStyleOption &other) const
{
    return rect == other.rect && font == other.font && fontMetrics == other.fontMetrics && palette == other.palette && padding == other.padding
        && horizontalMargin == other.horizontalMargin && verticalMargin == other.verticalMargin && iconSize == other.iconSize
        && extendedSelectionRegion == other.extendedSelectionRegion && maxTextLines == other.maxTextLines && maxTextWidth == other.maxTextWidth
        && verticalPadding == other.verticalPadding && lineHeightPercent == other.lineHeightPercent;
}

int KItemListStyleOption::effectiveVerticalPadding() const
{
    return verticalPadding < 0 ? padding : verticalPadding;
}

int KItemListStyleOption::scaledLineHeight(int fontLineHeight) const
{
    if (lineHeightPercent == 100) {
        return fontLineHeight;
    }
    return qMax(1, qRound(fontLineHeight * lineHeightPercent / 100.0));
}

bool KItemListStyleOption::operator!=(const KItemListStyleOption &other) const
{
    return !(*this == other);
}
