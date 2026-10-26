/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEFILEOPERATIONS_H
#define WHALEFILEOPERATIONS_H

#include "whaleviewapi_export.h"

#include <QFlags>
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
 * the modifier keys choose the action, otherwise the drop menu asks. Only items of the
 * context's items() take part: URLs of other items are ignored.
 *
 * Operations on items: for views whose items are not all in the listing, e.g. entries
 * deep below the location, which cannot be Whale's selection and therefore are not
 * reached by Whale's own actions (see WhaleViewCapability::RecursiveContent). The view
 * names the items explicitly, usually from its own context menu; they do not need to be
 * in items(). Whale carries the operations out with KDE's usual handling, as the file
 * list does: the same confirmations, dialogs, undo and messages in the view area. The
 * functions return at once; dialogs and jobs continue in the background. Operations on
 * several items are allowed where the file list allows them too.
 */
class WHALEVIEWAPI_EXPORT WhaleFileOperations : public QObject
{
    Q_OBJECT

public:
    /**
     * The operations on items.
     */
    enum class Operation : quint32 {
        Open = 1 << 0, ///< openItems()
        OpenWith = 1 << 1, ///< openItemsWith()
        ShowInFolder = 1 << 2, ///< showItemInFolder()
        Properties = 1 << 3, ///< showProperties()
        Rename = 1 << 4, ///< renameItems()
        Trash = 1 << 5, ///< trashItems()
        Delete = 1 << 6, ///< deleteItems()
        Copy = 1 << 7, ///< copyItems()
        Cut = 1 << 8, ///< cutItems()
        CopyLocation = 1 << 9, ///< copyLocations()
        Duplicate = 1 << 10, ///< duplicateItems()
    };
    Q_ENUM(Operation)
    Q_DECLARE_FLAGS(Operations, Operation)

    /**
     * How an operation that changes files ended (operationFinished()).
     */
    enum class Result {
        Done, ///< carried out
        Canceled, ///< the user canceled it (a confirmation, a dialog); nothing changed
        Failed, ///< an error, already shown to the user; some items may have changed
    };
    Q_ENUM(Result)

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

    /**
     * @return The operations that can be carried out on all of @p urls, by the rules of
     *         the file list's actions: e.g. no Rename, Trash, Delete or Cut without the
     *         permission to remove the items from their folder, no Trash for items that
     *         are not local, nothing but ShowInFolder for items that do not exist (any
     *         more). Operations that would remove the location of the view area, or a
     *         folder above it, are never offered. Empty for an empty list.
     */
    virtual Operations supportedOperations(const QList<QUrl> &urls) const = 0;

    /**
     * Opens items as the user does with Enter or a double click in the file list:
     * folders (and archives, if the user browses through them) become the location of
     * the view area, files are opened with their application (asking first for
     * programs and scripts). The view area becomes active.
     */
    virtual void openItems(const QList<QUrl> &urls) = 0;

    /**
     * Lets the user choose an application for the items in KDE's "Open With" dialog.
     */
    virtual void openItemsWith(const QList<QUrl> &urls) = 0;

    /**
     * Makes the folder that contains @p url the location of the view area and selects
     * @p url there, as Whale's selection (as "Open Path" does in the file list). The
     * selection is applied once the folder is loaded; if @p url is in items() already,
     * it is selected at once. The view area becomes active.
     */
    virtual void showItemInFolder(const QUrl &url) = 0;

    /**
     * Shows KDE's properties dialog of the items.
     */
    virtual void showProperties(const QList<QUrl> &urls) = 0;

    /**
     * Renames the items with the rename dialog of the file list (one name, or a pattern
     * for several items). Never inline. operationFinished() follows.
     */
    virtual void renameItems(const QList<QUrl> &urls) = 0;

    /**
     * Moves the items to the trash, with the confirmation the user chose for this and
     * with undo. Failures and a missing trash are handled as in the file list (an error
     * in the view area, or the offer to delete instead). operationFinished() follows.
     */
    virtual void trashItems(const QList<QUrl> &urls) = 0;

    /**
     * Deletes the items permanently, after the confirmation the user chose for this.
     * operationFinished() follows.
     */
    virtual void deleteItems(const QList<QUrl> &urls) = 0;

    /**
     * Puts the items on the clipboard to be copied, as Copy in the file list does; Whale's
     * Paste (or any other application) then copies them.
     */
    virtual void copyItems(const QList<QUrl> &urls) = 0;

    /**
     * Puts the items on the clipboard to be moved, as Cut in the file list does.
     */
    virtual void cutItems(const QList<QUrl> &urls) = 0;

    /**
     * Puts the paths of the items (local paths, else URLs) on the clipboard as text, one
     * per line, as "Copy Location" does.
     */
    virtual void copyLocations(const QList<QUrl> &urls) = 0;

    /**
     * Copies each item next to itself, named as "Duplicate" in the file list does
     * ("name copy.ext", numbered if that exists), with undo.
     */
    virtual void duplicateItems(const QList<QUrl> &urls) = 0;

Q_SIGNALS:
    /**
     * Is emitted when an operation that changes files (Rename, Trash, Delete), started
     * through this object, has ended. The view's items may have changed; a view that
     * shows more than the listing may want to scan again.
     * @param urls The items the operation was started for.
     * @param newUrls Rename: the new URLs of the renamed items; otherwise empty.
     */
    void operationFinished(WhaleFileOperations::Operation operation, const QList<QUrl> &urls, WhaleFileOperations::Result result, const QList<QUrl> &newUrls);

protected:
    explicit WhaleFileOperations(QObject *parent = nullptr);

private:
    const std::unique_ptr<WhaleFileOperationsPrivate> d;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(WhaleFileOperations::Operations)

#endif
