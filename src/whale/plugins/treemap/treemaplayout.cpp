/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "treemaplayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

QList<QRectF> TreemapLayout::squarify(const QList<qint64> &sizes, const QRectF &rect)
{
    const int count = sizes.count();
    QList<QRectF> result(count);
    double remaining = 0;
    for (const qint64 size : sizes) {
        remaining += double(size);
    }

    double x = rect.x();
    double y = rect.y();
    double width = rect.width();
    double height = rect.height();
    int begin = 0;
    while (begin < count) {
        if (begin == count - 1) {
            result[begin] = QRectF(x, y, width, height);
            break;
        }

        // A row goes along the shorter side of the remaining rectangle. It grows as long
        // as its worst aspect ratio improves (Bruls et al.).
        const bool column = width >= height; // the row is a column at the left
        const double side = column ? height : width;
        const double areaPerByte = (width * height) / remaining;
        double rowArea = 0;
        double rowMin = std::numeric_limits<double>::max();
        double rowMax = 0;
        double worst = std::numeric_limits<double>::max();
        int end = begin;
        while (end < count) {
            const double area = double(sizes.at(end)) * areaPerByte;
            const double newArea = rowArea + area;
            const double newMin = std::min(rowMin, area);
            const double newMax = std::max(rowMax, area);
            const double side2 = side * side;
            const double newWorst = std::max(side2 * newMax / (newArea * newArea), (newArea * newArea) / (side2 * newMin));
            if (end > begin && newWorst > worst) {
                break;
            }
            rowArea = newArea;
            rowMin = newMin;
            rowMax = newMax;
            worst = newWorst;
            ++end;
        }

        double rowBytes = 0;
        for (int i = begin; i < end; ++i) {
            rowBytes += double(sizes.at(i));
        }
        const bool last = end == count;
        // Positions are accumulated from fractions of sizes, and the last rectangle of a
        // row and the last row end exactly at the edge: neighbors share their edges, so
        // the rectangles tile without gaps or overlaps.
        if (column) {
            const double stripWidth = last ? width : width * (rowBytes / remaining);
            double position = y;
            double done = 0;
            for (int i = begin; i < end; ++i) {
                done += double(sizes.at(i));
                const double next = i == end - 1 ? y + height : y + height * (done / rowBytes);
                result[i] = QRectF(x, position, stripWidth, next - position);
                position = next;
            }
            x += stripWidth;
            width = last ? 0 : width - stripWidth;
        } else {
            const double stripHeight = last ? height : height * (rowBytes / remaining);
            double position = x;
            double done = 0;
            for (int i = begin; i < end; ++i) {
                done += double(sizes.at(i));
                const double next = i == end - 1 ? x + width : x + width * (done / rowBytes);
                result[i] = QRectF(position, y, next - position, stripHeight);
                position = next;
            }
            y += stripHeight;
            height = last ? 0 : height - stripHeight;
        }
        remaining -= rowBytes;
        begin = end;
    }
    return result;
}

QList<WhaleTreeNode> TreemapLayout::orderedChildren(const WhaleTreeNode &folder)
{
    QList<WhaleTreeNode> children = folder.children();
    children.erase(std::remove_if(children.begin(),
                                  children.end(),
                                  [](const WhaleTreeNode &child) {
                                      return child.totalSize() <= 0;
                                  }),
                   children.end());
    std::stable_sort(children.begin(), children.end(), [](const WhaleTreeNode &a, const WhaleTreeNode &b) {
        return a.totalSize() > b.totalSize();
    });
    return children;
}

void TreemapLayout::clear()
{
    m_cells.clear();
    m_bounds = QRectF();
    m_truncated = false;
}

void TreemapLayout::compute(const WhaleTreeNode &root, const QRectF &bounds, const Options &options)
{
    clear();
    m_bounds = bounds;
    if (root.isNull() || !bounds.isValid() || !std::isfinite(bounds.width()) || !std::isfinite(bounds.height()) || root.totalSize() <= 0) {
        return;
    }

    Cell rootCell;
    rootCell.node = root;
    rootCell.rect = bounds;
    rootCell.size = root.totalSize();
    m_cells.append(rootCell);

    QList<WhaleTreeNode> children;
    QList<qint64> sizes;
    // Breadth first: each cell is divided once; its children are appended together.
    for (int index = 0; index < m_cells.count(); ++index) {
        const Cell cell = m_cells.at(index);
        if (cell.isRest() || !cell.node.isDirectory() || cell.rect.width() < options.minDivideSide || cell.rect.height() < options.minDivideSide) {
            continue;
        }

        children = orderedChildren(cell.node);
        if (children.isEmpty()) {
            continue;
        }

        double total = 0;
        for (const WhaleTreeNode &child : std::as_const(children)) {
            total += double(child.totalSize());
        }
        const double areaPerByte = cell.rect.width() * cell.rect.height() / total;

        // The tail of children too small to be seen goes into one rest cell. A single
        // small child needs no rest cell of its own.
        int shown = children.count();
        while (shown > 1 && double(children.at(shown - 1).totalSize()) * areaPerByte < options.minCellArea) {
            --shown;
        }
        if (shown == children.count() - 1) {
            shown = children.count();
        }
        qint64 restSize = 0;
        for (int i = shown; i < children.count(); ++i) {
            restSize += children.at(i).totalSize();
        }

        const int newCells = shown + (restSize > 0 ? 1 : 0);
        if (m_cells.count() + newCells > options.maxCells) {
            m_truncated = true;
            break;
        }

        sizes.resize(0);
        for (int i = 0; i < shown; ++i) {
            sizes.append(children.at(i).totalSize());
        }
        if (restSize > 0) {
            sizes.append(restSize);
        }
        const QList<QRectF> rects = squarify(sizes, cell.rect);

        m_cells[index].firstChild = m_cells.count();
        m_cells[index].childCount = newCells;
        for (int i = 0; i < newCells; ++i) {
            Cell child;
            if (i < shown) {
                child.node = children.at(i);
            } else {
                child.restCount = children.count() - shown;
            }
            child.rect = rects.at(i);
            child.parent = index;
            child.depth = cell.depth + 1;
            child.size = sizes.at(i);
            m_cells.append(child);
        }
    }
}

int TreemapLayout::cellAt(const QPointF &point) const
{
    if (m_cells.isEmpty()) {
        return -1;
    }
    // Half-open rectangles, so a point on a shared edge belongs to one cell only.
    auto contains = [&point](const QRectF &rect) {
        return point.x() >= rect.left() && point.x() < rect.right() && point.y() >= rect.top() && point.y() < rect.bottom();
    };
    if (!contains(m_cells.first().rect)) {
        return -1;
    }
    int index = 0;
    while (true) {
        const Cell &cell = m_cells.at(index);
        int found = -1;
        for (int child = cell.firstChild; child >= 0 && child < cell.firstChild + cell.childCount; ++child) {
            if (contains(m_cells.at(child).rect)) {
                found = child;
                break;
            }
        }
        if (found < 0) {
            return index;
        }
        index = found;
    }
}

int TreemapLayout::cellFor(const QString &path) const
{
    if (m_cells.isEmpty()) {
        return -1;
    }
    const QString rootPath = m_cells.first().node.path();
    if (path == rootPath) {
        return 0;
    }
    const QString prefix = rootPath.endsWith(QLatin1Char('/')) ? rootPath : rootPath + QLatin1Char('/');
    if (!path.startsWith(prefix)) {
        return -1;
    }
    const QStringList parts = path.mid(prefix.length()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int index = 0;
    for (const QString &part : parts) {
        const Cell &cell = m_cells.at(index);
        int found = -1;
        for (int child = cell.firstChild; child >= 0 && child < cell.firstChild + cell.childCount; ++child) {
            const Cell &candidate = m_cells.at(child);
            if (!candidate.isRest() && candidate.node.name() == part) {
                found = child;
                break;
            }
        }
        if (found < 0) {
            return index;
        }
        index = found;
    }
    return index;
}
