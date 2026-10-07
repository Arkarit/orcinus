/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TREEMAPRENDERER_H
#define TREEMAPRENDERER_H

#include "treemaplayout.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPalette>

/**
 * @brief Paints a TreemapLayout as a cushion treemap (van Wijk, van de Wetering 1999).
 *
 * Each cell adds a parabolic ridge to a height field, in both directions, and the ridges
 * of deeper cells are lower. The leaves are shaded per pixel by the light falling onto
 * that surface, so every folder appears as a cushion that holds the cushions of its
 * entries: the hierarchy is visible without frames. Folders at the top levels get a thin
 * frame in addition, and names are written where they fit.
 *
 * The result is an image of the whole layout; hovering and selecting only paint on top
 * of it, so the image is only made again when the layout or an option changes.
 */
class TreemapRenderer
{
public:
    enum class ColorMode {
        /// Files are colored by their kind (images, audio, archives, ...).
        FileType,
        /// Everything below an entry of the location shares the color of that entry.
        TopFolder,
    };

    struct Options {
        ColorMode colorMode = ColorMode::FileType;
        bool showLabels = true;
        /// For TopFolder when the layout shows a folder below the location (zoomed in): the
        /// position of the location's entry it belongs to among the location's entries, as
        /// TopFolder colors them without zooming. -1: the layout root is the location.
        int topFolderPosition = -1;
        QFont font;
        QPalette palette;
        /// Drawn on every rest cell large enough, also without labels: a rest cell selects
        /// its folder. Without an icon an ellipsis is drawn.
        QIcon restIcon;
    };

    /**
     * @return An image of @p layout, of the size of the layout bounds times
     *         @p devicePixelRatio (the image carries that ratio).
     */
    QImage render(const TreemapLayout &layout, const Options &options, qreal devicePixelRatio);

    /**
     * @return The base color of @p cell (before shading).
     */
    QColor baseColor(const TreemapLayout &layout, int cell, const Options &options);

private:
    QColor fileTypeColor(const QString &name);

    // Colors per lowercase suffix, so the MIME database is asked once per suffix.
    QHash<QString, QColor> m_suffixColors;
};

#endif
