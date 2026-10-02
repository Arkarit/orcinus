/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinfileitemlistwidget.h"
#include "../kitemviews/private/kitemviewsutils.h"

#include "dolphindebug.h"
#include "dolphinitemlistview.h"
#include "filenamehighlighter.h"
#include "kitemviews/kfileitemmodel.h"

#include <KIconLoader>

namespace
{
const FileNameHighlighter *enabledFileNameHighlighter(const KItemListView *view)
{
    const auto dolphinView = qobject_cast<const DolphinItemListView *>(view);
    if (!dolphinView) {
        return nullptr;
    }
    const FileNameHighlighter *highlighter = dolphinView->fileNameHighlighter();
    return highlighter && highlighter->isEnabled() ? highlighter : nullptr;
}

KFileItem fileItemAt(const KItemListView *view, int index)
{
    const auto model = qobject_cast<const KFileItemModel *>(view ? view->model() : nullptr);
    return model ? model->fileItem(index) : KFileItem();
}
}

QFont DolphinFileItemListWidgetInformant::itemFont(int index, const KItemListView *view, const QFont &baseFont) const
{
    const FileNameHighlighter *highlighter = enabledFileNameHighlighter(view);
    if (!highlighter) {
        return KFileItemListWidgetInformant::itemFont(index, view, baseFont);
    }
    return highlighter->fontFor(fileItemAt(view, index), baseFont);
}

DolphinFileItemListWidget::DolphinFileItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent)
    : KFileItemListWidget(informant, parent)
{
}

DolphinFileItemListWidget::~DolphinFileItemListWidget() = default;

KItemListWidgetInformant *DolphinFileItemListWidget::createInformant()
{
    return new DolphinFileItemListWidgetInformant();
}

void DolphinFileItemListWidget::refreshCache()
{
    QColor color;
    bool hasVersionStateColor = false;
    const SmallHash values = data();
    QHash<Qt::Corner, QString> overlays;
    if (values.contains("version")) {
        // The item is under version control. Apply the text color corresponding
        // to its version state.
        const KVersionControlPlugin::ItemVersion version = static_cast<KVersionControlPlugin::ItemVersion>(values.value("version").toInt());
        hasVersionStateColor = version != KVersionControlPlugin::NormalVersion && version != KVersionControlPlugin::UnversionedVersion;
        const QColor textColor = styleOption().palette.text().color();
        QColor tintColor = textColor;

        // Using hardcoded colors is generally a bad idea. In this case the colors just act
        // as tint colors and are mixed with the current set text color. The tint colors
        // have been optimized for the base colors of the corresponding Oxygen emblems.
        switch (version) {
        case KVersionControlPlugin::UpdateRequiredVersion:
            tintColor = Qt::yellow;
            break;
        case KVersionControlPlugin::LocallyModifiedUnstagedVersion:
            tintColor = Qt::green;
            break;
        case KVersionControlPlugin::LocallyModifiedVersion:
            tintColor = Qt::green;
            break;
        case KVersionControlPlugin::AddedVersion:
            tintColor = Qt::green;
            break;
        case KVersionControlPlugin::RemovedVersion:
            tintColor = Qt::darkRed;
            break;
        case KVersionControlPlugin::ConflictingVersion:
            tintColor = Qt::red;
            break;
        case KVersionControlPlugin::IgnoredVersion:
            tintColor = Qt::white;
            break;
        case KVersionControlPlugin::MissingVersion:
            tintColor = Qt::red;
            break;
        case KVersionControlPlugin::NormalVersion:
        case KVersionControlPlugin::UnversionedVersion:
        default:
            break;
        }

        color = QColor((tintColor.red() + textColor.red()) / 2,
                       (tintColor.green() + textColor.green()) / 2,
                       (tintColor.blue() + textColor.blue()) / 2,
                       (tintColor.alpha() + textColor.alpha()) / 2);

        overlays.insert(Qt::Corner::TopLeftCorner, overlayForState(version));
    }

    if (values.contains("iconOverlays")) {
        const auto corners = {Qt::Corner::BottomLeftCorner, Qt::Corner::TopRightCorner, Qt::Corner::BottomRightCorner};
        const auto iconOverlays = values.value("iconOverlays").toStringList();
        auto overlaysIt = iconOverlays.constBegin();
        for (const auto &corner : corners) {
            if (overlaysIt == iconOverlays.constEnd()) {
                break;
            }
            overlays.insert(corner, *overlaysIt);
            overlaysIt = ++overlaysIt;
        }
    }

    setOverlays(overlays);
    setTextColor(color);

    // A color of the file name highlighting rules only applies to the name, and
    // must not hide a version control state.
    QColor nameColor;
    const FileNameHighlighter *highlighter = fileNameHighlighter();
    if (highlighter && !hasVersionStateColor) {
        const FileNameTextStyle style = highlighter->styleFor(fileItem(), FileNameHighlighter::Color);
        nameColor = highlighter->nameColor(style.color, styleOption().palette);
    }
    setNameTextColor(nameColor);

    setProperty(QByteArrayLiteral("iconSize"), styleOption().iconSize);
}

QFont DolphinFileItemListWidget::customizedFont(const QFont &baseFont) const
{
    const FileNameHighlighter *highlighter = fileNameHighlighter();
    if (!highlighter) {
        return KFileItemListWidget::customizedFont(baseFont);
    }
    return highlighter->fontFor(fileItem(), baseFont);
}

const FileNameHighlighter *DolphinFileItemListWidget::fileNameHighlighter() const
{
    return enabledFileNameHighlighter(qobject_cast<const KItemListView *>(parentWidget()));
}

KFileItem DolphinFileItemListWidget::fileItem() const
{
    return fileItemAt(qobject_cast<const KItemListView *>(parentWidget()), index());
}

QString DolphinFileItemListWidget::overlayForState(KVersionControlPlugin::ItemVersion version) const
{
    QString iconName;
    switch (version) {
    case KVersionControlPlugin::NormalVersion:
        iconName = QStringLiteral("vcs-normal");
        break;
    case KVersionControlPlugin::UpdateRequiredVersion:
        iconName = QStringLiteral("vcs-update-required");
        break;
    case KVersionControlPlugin::LocallyModifiedVersion:
        iconName = QStringLiteral("vcs-locally-modified");
        break;
    case KVersionControlPlugin::LocallyModifiedUnstagedVersion:
        iconName = QStringLiteral("vcs-locally-modified-unstaged");
        break;
    case KVersionControlPlugin::AddedVersion:
        iconName = QStringLiteral("vcs-added");
        break;
    case KVersionControlPlugin::RemovedVersion:
        iconName = QStringLiteral("vcs-removed");
        break;
    case KVersionControlPlugin::ConflictingVersion:
        iconName = QStringLiteral("vcs-conflicting");
        break;
    case KVersionControlPlugin::UnversionedVersion:
    case KVersionControlPlugin::IgnoredVersion:
    case KVersionControlPlugin::MissingVersion:
        break;
    default:
        Q_ASSERT(false);
        break;
    }

    return iconName;
}

#include "moc_dolphinfileitemlistwidget.cpp"
