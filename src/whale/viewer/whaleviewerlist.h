/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWERLIST_H
#define WHALEVIEWERLIST_H

#include <KFileItem>

#include <QStringList>

/**
 * @brief The files the built-in viewer pages through, in the order of the view (Whale).
 *
 * Only files the viewer can show (V-E2). Opened from one file, the list holds the files of its
 * folder as the view lists them (sorting, filter, hidden files), but none of expanded subfolders
 * (V-E4). Opened from several files (a multiple selection), it holds only those (V-E3).
 */
class WhaleViewerList
{
public:
    WhaleViewerList() = default;

    /**
     * @return Whether the viewer can show @p item: a local file whose MIME type (or one of its
     *         parents) is one of @p mimeTypes (see WhaleViewerSettings::mimeTypes()) and that Qt
     *         can decode, or that is a video (isVideo()).
     */
    static bool isDisplayable(const KFileItem &item, const QStringList &mimeTypes);

    /**
     * @return Whether @p item is a video the viewer plays (V3): a video type, built with libmpv,
     *         that Qt cannot decode as an image.
     */
    static bool isVideo(const KFileItem &item);

    /**
     * @return The list for @p opened: the displayable files among @p viewItems (the items of
     *         the view, in its order) that are in the folder of @p opened, starting at it. Just
     *         @p opened if the view does not list it.
     */
    static WhaleViewerList fromFolder(const KFileItemList &viewItems, const KFileItem &opened, const QStringList &mimeTypes);

    /**
     * @return The list for a multiple selection: the displayable files among @p items (in the
     *         order of the view), starting at @p current if it is one of them, else at the first.
     *         isSelection() is true. Empty if none is displayable.
     */
    static WhaleViewerList fromSelection(const KFileItemList &items, const QUrl &current, const QStringList &mimeTypes);

    bool isEmpty() const;
    int count() const;
    KFileItemList items() const;
    /** @return Whether the list was made from a multiple selection (fromSelection()). */
    bool isSelection() const;

    int currentIndex() const;
    KFileItem currentItem() const;
    void setCurrentIndex(int index);

    /**
     * Moves @p steps files on (backwards if negative). Without @p wrapAround it stops at the
     * first or last file; with it, it goes round.
     * @return Whether it stopped at the first or last file before it went all @p steps.
     */
    bool move(int steps, bool wrapAround);

private:
    KFileItemList m_items;
    int m_current = -1;
    bool m_selection = false;
};

#endif
