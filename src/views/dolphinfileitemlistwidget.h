/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOLPHINFILEITEMLISTWIDGET_H
#define DOLPHINFILEITEMLISTWIDGET_H

#include "dolphin_export.h"
#include "kitemviews/kfileitemlistwidget.h"
#include "versioncontrol/kversioncontrolplugin.h"

class FileNameHighlighter;
class KFileItem;

/**
 * @brief Extends KFileItemListWidgetInformant to calculate item sizes with
 *        the fonts of the file name highlighting rules.
 */
class DOLPHIN_EXPORT DolphinFileItemListWidgetInformant : public KFileItemListWidgetInformant
{
protected:
    QFont itemFont(int index, const KItemListView *view, const QFont &baseFont) const override;
};

/**
 * @brief Extends KFileItemListWidget to handle the "version" role and file name highlighting.
 *
 * The "version" role is set if version-control-plugins have been enabled.
 * @see KVersionControlPlugin
 * @see FileNameHighlighter
 */
class DOLPHIN_EXPORT DolphinFileItemListWidget : public KFileItemListWidget
{
    Q_OBJECT

public:
    DolphinFileItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent);
    ~DolphinFileItemListWidget() override;

    static KItemListWidgetInformant *createInformant();

protected:
    void refreshCache() override;
    QFont customizedFont(const QFont &baseFont) const override;

private:
    QString overlayForState(KVersionControlPlugin::ItemVersion version) const;

    /**
     * @return The enabled file name highlighter of the view, or nullptr.
     */
    const FileNameHighlighter *fileNameHighlighter() const;
    KFileItem fileItem() const;
};

#endif
