/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "treemaprenderer.h"

#include <QFontMetrics>
#include <QMimeDatabase>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <cmath>

namespace
{
// Cushion parameters of van Wijk and van de Wetering: the height of the root's ridges
// and how much lower the ridges of each further level are.
constexpr double RidgeHeight = 0.5;
constexpr double RidgeFactor = 0.75;
// Ambient and directional light; the light comes from the top left, mostly from the front.
constexpr double Ambient = 70;
constexpr double Diffuse = 185;
const double LightLength = std::sqrt(1.0 * 1.0 + 2.0 * 2.0 + 10.0 * 10.0);
const double LightX = -1.0 / LightLength;
const double LightY = -2.0 / LightLength;
const double LightZ = 10.0 / LightLength;

struct Surface {
    double x1 = 0; // linear coefficient in x
    double x2 = 0; // quadratic coefficient in x
    double y1 = 0;
    double y2 = 0;
};

// The icon on rest cells, in logical pixels.
constexpr int RestIconMinSize = 8;
constexpr int RestIconMaxSize = 24;

// @p icon in one @p color (its shape only), as symbolic icons are drawn on colored ground.
QPixmap monochrome(const QIcon &icon, int size, qreal devicePixelRatio, const QColor &color)
{
    QPixmap pixmap = icon.pixmap(QSize(size, size), devicePixelRatio);
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRectF(QPointF(0, 0), pixmap.deviceIndependentSize()), color);
    return pixmap;
}

void addRidge(double from, double to, double height, double &linear, double &quadratic)
{
    if (to - from <= 0) {
        return;
    }
    linear += 4 * height * (to + from) / (to - from);
    quadratic -= 4 * height / (to - from);
}
}

QColor TreemapRenderer::fileTypeColor(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const QString suffix = dot > 0 ? name.mid(dot + 1).toLower() : QString();
    const auto known = m_suffixColors.constFind(suffix);
    if (known != m_suffixColors.constEnd()) {
        return *known;
    }

    static const QStringList archives = {QStringLiteral("zip"),
                                         QStringLiteral("tar"),
                                         QStringLiteral("gz"),
                                         QStringLiteral("tgz"),
                                         QStringLiteral("xz"),
                                         QStringLiteral("bz2"),
                                         QStringLiteral("zst"),
                                         QStringLiteral("7z"),
                                         QStringLiteral("rar"),
                                         QStringLiteral("deb"),
                                         QStringLiteral("rpm"),
                                         QStringLiteral("iso"),
                                         QStringLiteral("img"),
                                         QStringLiteral("jar"),
                                         QStringLiteral("apk"),
                                         QStringLiteral("xpi")};
    QColor color(170, 155, 140); // other files
    if (archives.contains(suffix)) {
        color = QColor(205, 190, 90);
    } else if (!suffix.isEmpty()) {
        static const QMimeDatabase database;
        const QMimeType type = database.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
        const QString mime = type.name();
        if (mime.startsWith(QLatin1String("image/"))) {
            color = QColor(235, 160, 60);
        } else if (mime.startsWith(QLatin1String("video/"))) {
            color = QColor(215, 85, 95);
        } else if (mime.startsWith(QLatin1String("audio/"))) {
            color = QColor(165, 115, 215);
        } else if (mime == QLatin1String("application/pdf") || mime.contains(QLatin1String("opendocument")) || mime.contains(QLatin1String("officedocument"))
                   || mime.contains(QLatin1String("msword")) || mime.contains(QLatin1String("ms-excel")) || mime.contains(QLatin1String("ms-powerpoint"))) {
            color = QColor(90, 150, 225);
        } else if (mime.contains(QLatin1String("executable")) || mime.contains(QLatin1String("sharedlib")) || mime.contains(QLatin1String("object"))) {
            color = QColor(80, 185, 185);
        } else if (mime.startsWith(QLatin1String("text/")) || type.inherits(QStringLiteral("text/plain"))) {
            color = QColor(115, 195, 110);
        }
    }
    m_suffixColors.insert(suffix, color);
    return color;
}

QColor TreemapRenderer::baseColor(const TreemapLayout &layout, int index, const Options &options)
{
    const TreemapLayout::Cell &cell = layout.cells().at(index);
    if (cell.isRest()) {
        return QColor(175, 175, 175);
    }
    if (options.colorMode == ColorMode::TopFolder) {
        // Hues 137.5° apart (golden angle) keep neighbors in the order apart.
        auto hue = [](int position) {
            return QColor::fromHsv(int(std::fmod(25 + position * 137.508, 360.0)), 125, 235);
        };
        if (options.topFolderPosition >= 0) {
            return hue(options.topFolderPosition); // zoomed into one entry of the location
        }
        // The entry of the root this cell belongs to.
        int top = index;
        while (top > 0 && layout.cells().at(top).parent > 0) {
            top = layout.cells().at(top).parent;
        }
        if (top == 0) {
            return QColor(175, 175, 175);
        }
        if (!layout.cells().at(top).node.isDirectory()) {
            return QColor(165, 165, 170); // a file directly in the root
        }
        return hue(top - layout.cells().first().firstChild);
    }
    switch (cell.node.type()) {
    case WhaleTreeNode::Type::Directory:
        return QColor(140, 150, 175); // a folder shown without its content
    case WhaleTreeNode::Type::Symlink:
    case WhaleTreeNode::Type::Other:
        return QColor(210, 210, 210);
    case WhaleTreeNode::Type::File:
        break;
    }
    return fileTypeColor(cell.node.name());
}

QImage TreemapRenderer::render(const TreemapLayout &layout, const Options &options, qreal devicePixelRatio)
{
    const QRectF bounds = layout.bounds();
    const QSize pixels(std::max(1, int(std::ceil(bounds.width() * devicePixelRatio))), std::max(1, int(std::ceil(bounds.height() * devicePixelRatio))));
    QImage image(pixels, QImage::Format_RGB32);
    image.fill(options.palette.color(QPalette::Base));
    image.setDevicePixelRatio(devicePixelRatio);
    const QList<TreemapLayout::Cell> &cells = layout.cells();
    if (cells.isEmpty()) {
        return image;
    }

    // The surface of each cell: its parent's plus its own ridges, in device pixels.
    QList<Surface> surfaces(cells.count());
    const double ratio = devicePixelRatio;
    const QPointF origin = bounds.topLeft();
    for (int index = 0; index < cells.count(); ++index) {
        const TreemapLayout::Cell &cell = cells.at(index);
        Surface surface = cell.parent >= 0 ? surfaces.at(cell.parent) : Surface();
        const double height = RidgeHeight * std::pow(RidgeFactor, cell.depth);
        const QRectF rect = cell.rect.translated(-origin);
        addRidge(rect.left() * ratio, rect.right() * ratio, height, surface.x1, surface.x2);
        addRidge(rect.top() * ratio, rect.bottom() * ratio, height, surface.y1, surface.y2);
        surfaces[index] = surface;
    }

    // Shade the leaves pixel by pixel. A pixel belongs to the cell that contains its
    // center, so neighboring cells never paint the same pixel.
    for (int index = 0; index < cells.count(); ++index) {
        const TreemapLayout::Cell &cell = cells.at(index);
        if (!cell.isLeaf()) {
            continue;
        }
        const QRectF rect = cell.rect.translated(-origin);
        const int left = std::max(0, int(std::ceil(rect.left() * ratio - 0.5)));
        const int right = std::min(pixels.width(), int(std::ceil(rect.right() * ratio - 0.5)));
        const int top = std::max(0, int(std::ceil(rect.top() * ratio - 0.5)));
        const int bottom = std::min(pixels.height(), int(std::ceil(rect.bottom() * ratio - 0.5)));
        if (left >= right || top >= bottom) {
            continue;
        }
        const QColor color = baseColor(layout, index, options);
        const double red = color.red();
        const double green = color.green();
        const double blue = color.blue();
        if (cell.isRest()) {
            // Flat, unlike the cushions of entries: it stands for several small entries.
            const double light = (Ambient + Diffuse * LightZ) / 220.0;
            const QRgb flat = qRgb(std::min(255, int(red * light)), std::min(255, int(green * light)), std::min(255, int(blue * light)));
            for (int y = top; y < bottom; ++y) {
                std::fill_n(reinterpret_cast<QRgb *>(image.scanLine(y)) + left, right - left, flat);
            }
            continue;
        }
        const Surface &surface = surfaces.at(index);
        for (int y = top; y < bottom; ++y) {
            const double ny = -(2 * surface.y2 * (y + 0.5) + surface.y1);
            auto line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = left; x < right; ++x) {
                const double nx = -(2 * surface.x2 * (x + 0.5) + surface.x1);
                const double cosine = (nx * LightX + ny * LightY + LightZ) / std::sqrt(nx * nx + ny * ny + 1.0);
                const double light = (Ambient + std::max(0.0, Diffuse * cosine)) / 220.0;
                line[x] = qRgb(std::min(255, int(red * light)), std::min(255, int(green * light)), std::min(255, int(blue * light)));
            }
        }
    }

    QPainter painter(&image);
    painter.translate(-origin);

    // Thin frames around the folders of the first two levels separate them clearly.
    for (int index = 1; index < cells.count(); ++index) {
        const TreemapLayout::Cell &cell = cells.at(index);
        if (cell.isLeaf() || cell.depth > 2) {
            continue;
        }
        painter.setPen(QPen(QColor(0, 0, 0, cell.depth == 1 ? 130 : 70), 0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(cell.rect.adjusted(0, 0, -1.0 / ratio, -1.0 / ratio));
    }

    // The rest cells select their folder: always marked as such, also without labels.
    const QFontMetrics restMetrics(options.font);
    for (int index = 1; index < cells.count(); ++index) {
        const TreemapLayout::Cell &cell = cells.at(index);
        if (!cell.isRest()) {
            continue;
        }
        const int extent = int(std::min(cell.rect.width(), cell.rect.height()));
        if (options.restIcon.isNull()) {
            if (cell.rect.width() >= restMetrics.horizontalAdvance(QStringLiteral("…")) + 4 && cell.rect.height() >= restMetrics.height()) {
                painter.setFont(options.font);
                painter.setPen(QColor(0, 0, 0, 150));
                painter.drawText(cell.rect.translated(1, 1), Qt::AlignCenter, QStringLiteral("…"));
                painter.setPen(Qt::white);
                painter.drawText(cell.rect, Qt::AlignCenter, QStringLiteral("…"));
            }
            continue;
        }
        if (extent < RestIconMinSize + 4) {
            continue;
        }
        const int size = std::min(RestIconMaxSize, extent * 3 / 5);
        const QRectF box(cell.rect.center().x() - size / 2.0, cell.rect.center().y() - size / 2.0, size, size);
        painter.drawPixmap(box.translated(1, 1).topLeft(), monochrome(options.restIcon, size, ratio, QColor(0, 0, 0, 150)));
        painter.drawPixmap(box.topLeft(), monochrome(options.restIcon, size, ratio, Qt::white));
    }

    if (options.showLabels) {
        QFont font = options.font;
        const QFontMetrics metrics(font);
        QFont folderFont = font;
        folderFont.setBold(true);
        const QFontMetrics folderMetrics(folderFont);
        const int lineHeight = metrics.height();
        // Names never cover each other: the folders of the upper levels come first (the
        // cells are in breadth-first order), and a later name that would overlap is left out.
        QList<QRectF> taken;
        auto isFree = [&taken](const QRectF &box) {
            return std::none_of(taken.cbegin(), taken.cend(), [&box](const QRectF &other) {
                return other.intersects(box);
            });
        };
        for (int index = 1; index < cells.count(); ++index) {
            const TreemapLayout::Cell &cell = cells.at(index);
            if (cell.rect.width() < 24 || cell.rect.height() < lineHeight + 2) {
                continue;
            }
            if (cell.isRest()) {
                continue; // marked above
            }
            const QString name = cell.node.name();
            if (cell.isLeaf()) {
                const QString text = metrics.elidedText(name, Qt::ElideMiddle, int(cell.rect.width() - 6));
                // A name shortened to a few letters says nothing; then none.
                if (text.isEmpty() || (text != name && text.length() < 7)) {
                    continue;
                }
                const qreal width = metrics.horizontalAdvance(text);
                const QRectF box(cell.rect.center().x() - width / 2, cell.rect.center().y() - lineHeight / 2.0, width, lineHeight);
                if (!isFree(box)) {
                    continue;
                }
                taken.append(box);
                painter.setFont(font);
                painter.setPen(QColor(0, 0, 0, 150));
                painter.drawText(cell.rect.translated(1, 1), Qt::AlignCenter, text);
                painter.setPen(Qt::white);
                painter.drawText(cell.rect, Qt::AlignCenter, text);
            } else if (cell.depth <= 2) {
                const QString text = folderMetrics.elidedText(name, Qt::ElideMiddle, int(cell.rect.width() - 10));
                if (text.isEmpty() || (text != name && text.length() < 5)) {
                    continue;
                }
                const QRectF box(cell.rect.left() + 2, cell.rect.top() + 2, folderMetrics.horizontalAdvance(text) + 6, folderMetrics.height());
                if (!isFree(box)) {
                    continue;
                }
                taken.append(box);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 120));
                painter.drawRoundedRect(box, 3, 3);
                painter.setFont(folderFont);
                painter.setPen(Qt::white);
                painter.drawText(box, Qt::AlignCenter, text);
            }
        }
    }
    return image;
}
