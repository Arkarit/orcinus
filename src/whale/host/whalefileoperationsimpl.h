/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEFILEOPERATIONSIMPL_H
#define WHALEFILEOPERATIONSIMPL_H

#include "whalefileoperations.h"

#include <KFileItem>

#include <functional>

class DolphinViewBridge;
class QDrag;
class QMimeData;

/**
 * @brief The WhaleFileOperations of one view, carried out by the DolphinView of its
 * view area through the DolphinViewBridge.
 *
 * Drag data and drop handling are the DolphinView's own (see DolphinViewBridge), so a
 * plugin view drags and drops exactly like the file list.
 */
class WhaleFileOperationsImpl : public WhaleFileOperations
{
    Q_OBJECT

public:
    explicit WhaleFileOperationsImpl(DolphinViewBridge *bridge, QObject *parent = nullptr);
    ~WhaleFileOperationsImpl() override;

    Qt::DropAction startDrag(const QList<QUrl> &urls, const QPixmap &pixmap = QPixmap()) override;
    bool updateDropAction(QDragMoveEvent *event, const QUrl &url) override;
    bool drop(QDropEvent *event, const QUrl &url, QWidget *widget) override;

    Operations supportedOperations(const QList<QUrl> &urls) const override;
    void openItems(const QList<QUrl> &urls) override;
    void openItemsWith(const QList<QUrl> &urls) override;
    void showItemInFolder(const QUrl &url) override;
    void showProperties(const QList<QUrl> &urls) override;
    void renameItems(const QList<QUrl> &urls) override;
    void trashItems(const QList<QUrl> &urls) override;
    void deleteItems(const QList<QUrl> &urls) override;
    void copyItems(const QList<QUrl> &urls) override;
    void cutItems(const QList<QUrl> &urls) override;
    void copyLocations(const QList<QUrl> &urls) override;
    void duplicateItems(const QList<QUrl> &urls) override;

    using DragRunner = std::function<Qt::DropAction(QDrag *drag)>;

    /**
     * Development tools only (the dev MCP automation): runs the drags of all views with
     * @p runner instead of QDrag::exec(), which needs a real pointer. The runner gets
     * the drag as Whale built it and returns the drop action. An empty runner restores
     * QDrag::exec().
     */
    static void setDragRunner(const DragRunner &runner);

    /**
     * @return The data of the drag a view started through Whale that is running now, else
     *         null; a view without a destination refuses its own drag without a word.
     */
    static const QMimeData *currentDragData();

    /**
     * Moving to the trash, deleting and cutting are always confirmed by the user, with the
     * items listed (a view whose deep selection the user may not see completely, e.g. a
     * treemap). Off by default: the user's settings for the trash and deleting apply.
     */
    void setConfirmRemoval(bool confirm);
    bool confirmRemoval() const;

    using ConfirmationRunner = std::function<bool(Operation operation, const QList<QUrl> &urls)>;
    /**
     * Tests and development tools only: answers the confirmations of setConfirmRemoval()
     * with @p runner (true: confirmed) instead of asking the user; a confirmed removal then
     * follows the user's settings for the trash and deleting. An empty runner asks again.
     */
    static void setConfirmationRunner(const ConfirmationRunner &runner);

private:
    /// The items of @p urls: as listed, else read from the file system (KFileItem(url)).
    KFileItemList itemsFor(const QList<QUrl> &urls) const;
    /// False after showing an error in the view area if a local item does not exist.
    bool checkExisting(const QList<QUrl> &urls) const;
    /// True if one of @p urls is the location of the view area or a folder above it.
    bool removesLocation(const QList<QUrl> &urls) const;
    void putOnClipboard(const QList<QUrl> &urls, bool cut) const;
    /// Moves to the trash or deletes, as DolphinView::trashSelectedItems() and deleteSelectedItems().
    void removeItems(Operation operation, const QList<QUrl> &urls);
    /// Cuts @p urls if the user confirms it (setConfirmRemoval()).
    void confirmCut(const QList<QUrl> &urls);

    DolphinViewBridge *const m_bridge;
    bool m_confirmRemoval = false;
};

#endif
