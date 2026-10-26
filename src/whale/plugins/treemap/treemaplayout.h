/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TREEMAPLAYOUT_H
#define TREEMAPLAYOUT_H

#include "whaletreenode.h"

#include <QList>
#include <QRectF>

/**
 * @brief Squarified treemap layout (Bruls, Huizing, van Wijk 2000) of a WhaleTreeNode tree.
 *
 * Every entry with a size gets a rectangle whose area is proportional to its
 * WhaleTreeNode::totalSize(), inside the rectangle of its folder. The children of a
 * folder fill its rectangle completely and do not overlap; entries of size 0 get no
 * rectangle. The result only depends on the tree and the bounds: children are laid out
 * by size (largest first) and, for equal sizes, in the scanner's name order.
 *
 * Entries too small to be seen are not laid out one by one: the smallest children of
 * a folder whose rectangles would be smaller than Options::minCellArea share one "rest"
 * cell with their total size. So the areas stay proportional, while the number of cells
 * is bounded by the area of the bounds and not by the size of the tree.
 *
 * Cells are stored in breadth-first order: a cell comes after its parent, and the
 * children of a cell are stored next to each other. The layout knows nothing about
 * painting; see TreemapRenderer.
 */
class TreemapLayout
{
public:
    struct Options {
        /// Children whose rectangle would be smaller than this area (in units of the bounds)
        /// are joined into one rest cell. At least one child of a folder is always laid out.
        qreal minCellArea = 12.0;
        /// Cells narrower or lower than this are not divided into their children.
        qreal minDivideSide = 3.0;
        /// Upper limit of the number of cells, a safety net for huge bounds.
        int maxCells = 400000;
    };

    struct Cell {
        /// The entry, or a null node for a rest cell.
        WhaleTreeNode node;
        QRectF rect;
        /// Index of the parent cell, -1 for the root.
        int parent = -1;
        /// Index of the first child cell; the children are stored next to each other.
        int firstChild = -1;
        int childCount = 0;
        /// 0 for the root of the layout.
        int depth = 0;
        /// The totalSize() of the entry, or of all entries of a rest cell.
        qint64 size = 0;
        /// For a rest cell: the number of entries it stands for.
        int restCount = 0;

        bool isRest() const
        {
            return node.isNull();
        }
        bool isLeaf() const
        {
            return childCount == 0;
        }
    };

    TreemapLayout() = default;

    /**
     * Lays out @p root in @p bounds. Without a root, with empty bounds or for a root
     * of size 0 there are no cells.
     */
    void compute(const WhaleTreeNode &root, const QRectF &bounds, const Options &options);
    void compute(const WhaleTreeNode &root, const QRectF &bounds)
    {
        compute(root, bounds, Options());
    }
    void clear();

    const QList<Cell> &cells() const
    {
        return m_cells;
    }
    QRectF bounds() const
    {
        return m_bounds;
    }
    /**
     * @return True if Options::maxCells stopped the layout before all cells were divided.
     */
    bool isTruncated() const
    {
        return m_truncated;
    }

    /**
     * @return The index of the deepest cell at @p point, or -1 outside of the layout.
     */
    int cellAt(const QPointF &point) const;
    /**
     * @return The index of the deepest cell that shows @p path or a folder above it
     *         (relative to the layout root's path), or -1 if @p path is not below the root.
     *         Entries in a rest cell resolve to their folder's cell.
     */
    int cellFor(const QString &path) const;

    /**
     * @return The children of @p folder with a size, in the order the layout uses:
     *         largest first, equal sizes in the scanner's name order.
     */
    static QList<WhaleTreeNode> orderedChildren(const WhaleTreeNode &folder);

    /**
     * Divides @p rect among @p sizes (all > 0) in the order given, squarified.
     * The rectangles fill @p rect completely; exposed for tests.
     */
    static QList<QRectF> squarify(const QList<qint64> &sizes, const QRectF &rect);

private:
    QList<Cell> m_cells;
    QRectF m_bounds;
    bool m_truncated = false;
};

#endif
