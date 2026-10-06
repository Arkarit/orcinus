/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOLPHINVIEWBRIDGE_H
#define DOLPHINVIEWBRIDGE_H

#include "whaleselection.h"

#include <KFileItem>

#include <QObject>
#include <QUrl>

#include <optional>

class DolphinView;
class KFileItemModel;
class KItemListController;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QTimer;
class WhaleItemList;

/**
 * @brief Whale's only access to the internals of a DolphinView.
 *
 * Plugin views show the content of the hidden DolphinView of their view area: it
 * keeps loading the location, and its KFileItemModel stays the only source of the
 * items. The bridge translates that state into the types of the plugin API, so no
 * other Whale class needs to know DolphinView internals or kitemviews.
 *
 * There is one bridge per view area (WhaleViewHost). It must outlive the item
 * lists it creates.
 *
 * Selection: the DolphinView (its KItemListSelectionManager) stays the only store of
 * the selection of the view area. The bridge translates between its indexes and
 * URLs in both directions; plugin views only show a copy. So the selection survives
 * switching views, and everything in the shell that reads the DolphinView's
 * selection keeps working.
 */
class DolphinViewBridge : public QObject
{
    Q_OBJECT

public:
    explicit DolphinViewBridge(DolphinView *dolphinView, QObject *parent = nullptr);
    ~DolphinViewBridge() override;

    QUrl url() const;

    /**
     * @return True while the DolphinView loads its location.
     */
    bool isLoading() const;

    /**
     * @return A new list that mirrors the items of the DolphinView in their order
     *         until the list is destroyed. The caller owns it through @p parent.
     */
    WhaleItemList *createItemList(QObject *parent) const;

    /**
     * @return The selection of the DolphinView, in model order.
     */
    WhaleSelection selection() const;

    /**
     * @return True if at least one item is selected. Cheaper than selection().
     */
    bool hasSelection() const;

    /**
     * Sets the selection of the DolphinView directly in its selection manager, so the
     * usual DolphinView signals follow (status bar, information panel, actions).
     * URLs that are not in the listing are ignored; an empty current URL keeps the
     * current item.
     * @return The resulting selection.
     */
    WhaleSelection setSelection(const WhaleSelection &selection);

    /**
     * Activates the items like Enter or a double click in the DolphinView: the view
     * area becomes active, and the DolphinView's own handling opens them. URLs that
     * are not in the listing are ignored.
     */
    void activateItems(const QList<QUrl> &urls);

    /**
     * Makes the folder @p url the location of the view area, through the shell's
     * handling of activated URLs (DolphinView::urlActivated, as for a link in a tooltip):
     * the view area becomes active and the shell enters the folder there, with an entry in
     * the history. Local URLs that are not folders are ignored.
     */
    void openFolder(const QUrl &url);
    /**
     * Opens the DolphinView's context menu (DolphinContextMenu, KFileItemActions,
     * service menus) at @p globalPos for @p url and the selection, or for the location
     * if @p url is empty. An item outside the selection becomes the selection first,
     * as on a right click in the DolphinView. Activates the view area.
     */
    void requestContextMenu(const QPoint &globalPos, const QUrl &url);

    /**
     * Reports the item under the pointer to the shell (information panel, status
     * bar); an empty @p url clears it. Items of the listing are reported as listed; an
     * existing local file or folder outside the listing (e.g. deep below the location)
     * as a KFileItem made from the file system. Other URLs clear it. Does not activate
     * the view area, as hovering does not in the DolphinView either.
     */
    void requestItemInfo(const QUrl &url);

    /**
     * Opens @p items like a double click in the DolphinView (plan 3.2): the view area
     * becomes active and the container's own handling enters folders and archives or
     * opens files with their application. The items need not be in the listing.
     */
    void openItems(const KFileItemList &items);

    /**
     * Makes the folder of @p url the location and selects @p url there, as Dolphin's
     * "Open Path" does: the view area becomes active, the shell enters the folder
     * (DolphinView::urlActivated, with history), and the item is marked for selection
     * once the DolphinView has the new location, so loading the folder selects it. If
     * the folder is the location already, the item is selected at once.
     */
    void showItemInFolder(const QUrl &url);

    /**
     * Duplicates @p items with the DolphinView's own "Duplicate" (names, undo, errors in
     * the view area). The items need not be in the listing.
     */
    void duplicateItems(const KFileItemList &items);

    /**
     * Shows @p message as an error in the view area, as the DolphinView does after a
     * failed operation (DolphinView::errorMessage).
     */
    void showErrorMessage(const QString &message, int kioErrorCode);

    /**
     * Shows @p message in the status bar of the view area, as the DolphinView does after
     * an operation (DolphinView::operationCompletedMessage).
     */
    void showOperationCompletedMessage(const QString &message);

    /**
     * @return The widget that dialogs and jobs of the view area belong to, as those of
     *         the DolphinView do (it is the DolphinView, hidden or not).
     */
    QWidget *operationsWidget() const;

    /**
     * Renames the selected items with Dolphin's rename dialog (KIO::RenameFileDialog,
     * also for several items), never with the inline editor of the DolphinView,
     * which is hidden while a plugin view is shown. Does nothing without a selection.
     */
    void renameSelectedItemsWithDialog();

    /**
     * @return The item @p url of the listing, or a null item if it is not listed.
     */
    KFileItem item(const QUrl &url) const;

    /**
     * @return True if the user wants previews at the location ("Show Previews", a view
     *         property of the DolphinView).
     */
    bool previewsShown() const;

    /**
     * @return The drag data of the listed items among @p urls, exactly as the
     *         DolphinView puts it into its own drags (KFileItemModel::createMimeData(),
     *         exported to the desktop portal); null if none of them is listed. The
     *         caller owns it.
     */
    QMimeData *createDragMimeData(const QList<QUrl> &urls) const;

    /**
     * Accepts or ignores a drag over the item @p url (the location if empty) with the
     * rules of the DolphinView's controller, and sets the event's drop action.
     * @return True if accepted.
     */
    bool updateDropAction(QDragMoveEvent *event, const QUrl &url) const;

    /**
     * Drops @p event onto the item @p url (the location if empty) as a drop onto the
     * DolphinView: its target rules, then DolphinView::dropUrls() (KIO::drop with the
     * drop menu, error messages, selecting the items dropped into the location), then
     * the view area becomes active. The drop menu shows up at the event's position in
     * @p widget.
     * @return False if @p url is neither empty nor listed.
     */
    bool drop(QDropEvent *event, const QUrl &url, QWidget *widget);

    /**
     * F1: the DolphinView's model shows the items given by setExternalItems() instead of the
     * location's listing (KFileItemModel::setExternalItemsEnabled()), e.g. the files below
     * the location. Enabling empties the file list; disabling shows the listing again.
     */
    void setExternalItemsEnabled(bool enabled);
    bool externalItemsEnabled() const;

    /**
     * F1: the items of the file list while external items are enabled
     * (KFileItemModel::setExternalItems()); loading the location completes with them.
     */
    void setExternalItems(const KFileItemList &items);

    /**
     * @return The widget that receives drags over the file list: the viewport of its
     *         QGraphicsView (KItemListContainer).
     */
    QWidget *fileListDragWidget() const;

Q_SIGNALS:
    void urlChanged(const QUrl &url);
    void loadingStarted();
    void loadingCompleted();
    void loadingCanceled();

    /**
     * Is emitted once the event loop is idle after the selection has changed, whoever
     * changed it. Steps in between that end in the previous selection are not reported.
     */
    void selectionChanged(const WhaleSelection &selection);

    /**
     * Is emitted immediately when the selection becomes empty or not empty, e.g. for
     * enabling actions before the next key press.
     */
    void hasSelectionChanged(bool hasSelection);

    /**
     * previewsShown() has changed, by the action or by entering a location with other
     * view properties.
     */
    void previewsShownChanged(bool shown);

private:
    KItemListController *controller() const;
    KFileItemModel *model() const;
    void reportSelection();
    void slotUrlChangedForReveal(const QUrl &url);
    void slotLoadingCompletedForReveal();

    DolphinView *const m_dolphinView;
    QTimer *m_selectionTimer;
    std::optional<WhaleSelection> m_reportedSelection;
    bool m_hasSelection = false;
    // showItemInFolder(): the item to select, and the folder it waits for.
    QUrl m_revealUrl;
    QUrl m_revealFolder;
    bool m_revealFolderShown = false;
    // F1: the location is (re)loaded and its external items are still to come.
    bool m_externalItemsPending = false;
};

#endif
