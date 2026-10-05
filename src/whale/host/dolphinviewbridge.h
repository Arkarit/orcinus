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
     * @return A new list that mirrors the items of the DolphinView in their order
     *         until the list is destroyed. The caller owns it through @p parent.
     */
    WhaleItemList *createItemList(QObject *parent) const;

    /**
     * @return The selection of the DolphinView, in model order.
     */
    WhaleSelection selection() const;

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

private:
    KItemListController *controller() const;
    KFileItemModel *model() const;
    void reportSelection();

    DolphinView *const m_dolphinView;
    QTimer *m_selectionTimer;
    std::optional<WhaleSelection> m_reportedSelection;
};

#endif
