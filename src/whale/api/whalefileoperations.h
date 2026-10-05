/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEFILEOPERATIONS_H
#define WHALEFILEOPERATIONS_H

#include "whaleviewapi_export.h"

#include <QList>
#include <QObject>
#include <QPixmap>
#include <QUrl>

#include <memory>

class QDragMoveEvent;
class QDropEvent;
class QWidget;
class WhaleFileOperationsPrivate;

/**
 * @brief File operations of a view that Whale carries out as in the file list.
 *
 * Drag and drop: the view only recognizes the gestures and the items under the
 * pointer. Whale provides the drag data (the same as the file list: URLs, the local
 * paths of files, and the export to the desktop portal for sandboxed applications),
 * decides where a drop goes, and copies, moves or links with KDE's drop handling:
 * the modifier keys choose the action, otherwise the drop menu asks.
 *
 * Only items of the context's items() take part: URLs of other items are ignored.
 */
class WHALEVIEWAPI_EXPORT WhaleFileOperations : public QObject
{
    Q_OBJECT

public:
    ~WhaleFileOperations() override;

    /**
     * Drags the items @p urls, as the user drags items in the file list; usually the
     * selected items, from the view's QAbstractItemView::startDrag() or after its own
     * drag gesture. Returns when the drag has ended (QDrag::exec()).
     * @param pixmap The image under the pointer while dragging; if null, the icon of
     *        the first item.
     * @return The action of the drop target, Qt::IgnoreAction if there was no drop or
     *         no URL is in items().
     */
    virtual Qt::DropAction startDrag(const QList<QUrl> &urls, const QPixmap &pixmap = QPixmap()) = 0;

    /**
     * For dragEnterEvent() and dragMoveEvent(): accepts or ignores @p event for a drop
     * onto the item @p url, or onto the location if @p url is empty (the view's
     * background), with the rules of the file list: a writable folder, a desktop file
     * or a program can be the target, a folder never onto itself or one of the dragged
     * items. Sets the event's drop action.
     * @return True if the event was accepted.
     */
    virtual bool updateDropAction(QDragMoveEvent *event, const QUrl &url) = 0;

    /**
     * For dropEvent(): drops the data of @p event onto the item @p url, or onto the
     * location if @p url is empty. A folder, desktop file or program among the items
     * receives the drop; any other item stands for the location. Items dropped into the
     * location become the selection, and the view area becomes active.
     * @param widget The widget that got the event: the drop menu shows up at the
     *        event's position in it.
     * @return False if nothing was dropped, e.g. because @p url is not in items().
     */
    virtual bool drop(QDropEvent *event, const QUrl &url, QWidget *widget) = 0;

protected:
    explicit WhaleFileOperations(QObject *parent = nullptr);

private:
    const std::unique_ptr<WhaleFileOperationsPrivate> d;
};

#endif
