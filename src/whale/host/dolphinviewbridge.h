/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOLPHINVIEWBRIDGE_H
#define DOLPHINVIEWBRIDGE_H

#include "whaleselection.h"

#include <QObject>
#include <QUrl>

#include <optional>

class DolphinView;
class KFileItemModel;
class KItemListController;
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
     * Opens the DolphinView's context menu (DolphinContextMenu, KFileItemActions,
     * service menus) at @p globalPos for @p url and the selection, or for the location
     * if @p url is empty. An item outside the selection becomes the selection first,
     * as on a right click in the DolphinView. Activates the view area.
     */
    void requestContextMenu(const QPoint &globalPos, const QUrl &url);

    /**
     * Reports the item under the pointer to the shell (information panel, status
     * bar); an empty @p url clears it. Does not activate the view area, as hovering
     * does not in the DolphinView either.
     */
    void requestItemInfo(const QUrl &url);

    /**
     * Renames the selected items with Dolphin's rename dialog (KIO::RenameFileDialog,
     * also for several items), never with the inline editor of the DolphinView,
     * which is hidden while a plugin view is shown. Does nothing without a selection.
     */
    void renameSelectedItemsWithDialog();

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

private:
    KItemListController *controller() const;
    KFileItemModel *model() const;
    void reportSelection();

    DolphinView *const m_dolphinView;
    QTimer *m_selectionTimer;
    std::optional<WhaleSelection> m_reportedSelection;
    bool m_hasSelection = false;
};

#endif
