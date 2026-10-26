/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALECOMMANDROUTER_H
#define WHALECOMMANDROUTER_H

#include "whalefileoperations.h"

#include <KFileItem>

#include <QList>
#include <QUrl>

class DolphinView;
class WhaleViewHost;

/**
 * @brief Where Whale's actions on selected items get their selection from (T2).
 *
 * The actions (Delete, F2, Copy, Properties, ...) work on the active view area, which
 * Dolphin knows by its DolphinView. Usually that DolphinView's selection is the one: in
 * the file list, and in plugin views that show Whale's selection (it is mirrored into the
 * DolphinView). A plugin view with RecursiveContent (e.g. the treemap) has a selection of
 * its own instead (WhaleView::selectedItems()), possibly deep below the location; it is
 * never put into the DolphinView. For such a view area the actions are routed: Dolphin's
 * code asks this class for the items, and the operations go to the view's
 * WhaleFileOperations with exactly those items.
 *
 * Only ever the view area of the DolphinView in question is asked: the selections of the
 * two halves of a split view are never mixed.
 */
class WhaleCommandRouter
{
public:
    enum class Source {
        FileList, ///< the DolphinView itself is shown
        MirroredPluginView, ///< a plugin view that shows Whale's (the DolphinView's) selection
        PluginView, ///< a plugin view with a selection of its own: actions are routed
    };

    /**
     * @return The host of the view area of @p view, or null (e.g. a DolphinView outside Whale).
     */
    static WhaleViewHost *hostOf(const DolphinView *view);

    static Source source(const DolphinView *view);
    static QString sourceName(Source source);

    /**
     * @return True if the actions on selected items of the view area of @p view are routed
     *         to its plugin view.
     */
    static bool routes(const DolphinView *view);

    /**
     * @return The selected items for the actions of the view area of @p view: the plugin
     *         view's own selection if it is routed, else the DolphinView's selection.
     */
    static QList<QUrl> selectedUrls(const DolphinView *view);

    /**
     * selectedUrls() as file items, for Dolphin's code that decides about or acts on the
     * selection (action states, copying to the other half of a split view). Items of the
     * listing are taken from it, others read from the file system.
     */
    static KFileItemList selectedItems(const DolphinView *view);

    /**
     * Carries out @p operation on the routed selection of the view area of @p view.
     * @return False if that view area is not routed: Dolphin's own path applies. True
     *         otherwise, also when nothing was done because nothing is selected or
     *         @p operation is not supported for the selection; Properties without a
     *         selection is left to Dolphin (the location's properties).
     */
    static bool perform(DolphinView *view, WhaleFileOperations::Operation operation);
};

#endif
