/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALEITEMLIST_H
#define WHALEITEMLIST_H

#include "whaleviewapi_export.h"

#include <KFileItem>

#include <QList>
#include <QObject>

#include <memory>

class WhaleItemListPrivate;

/**
 * @brief The items of the location shown in a view area, in the host's order.
 *
 * The list contains the items the host shows for the current location: sorted and
 * filtered as set up by the user (sort role and order, hidden files, name filter).
 * A view that shows the items in this order follows these settings without any
 * logic of its own.
 *
 * The list is read-only for views. It changes in small steps, each reported by one
 * signal that is emitted after the list has changed, so item() already returns the
 * new state while the signal is delivered. The indexes of a signal refer to the
 * list as it was right before that step.
 */
class WHALEVIEWAPI_EXPORT WhaleItemList : public QObject
{
    Q_OBJECT

public:
    ~WhaleItemList() override;

    int count() const;
    bool isEmpty() const;

    /**
     * @return The item at @p index, or a null item if @p index is out of range.
     */
    KFileItem item(int index) const;

    /**
     * @return All items in order.
     */
    KFileItemList items() const;

Q_SIGNALS:
    /**
     * The whole content has been replaced. Re-read it with items().
     */
    void itemsReset();

    /**
     * @p items have been inserted at @p index, which is the index of the first
     * new item.
     */
    void itemsInserted(int index, const KFileItemList &items);

    /**
     * @p items, which started at @p index, have been removed.
     */
    void itemsRemoved(int index, const KFileItemList &items);

    /**
     * The items in the range starting at @p index have been reordered, for example
     * because the sorting has changed. The item that was at index + i is now at
     * @p movedToIndexes[i]. The indexes form a permutation of the range, so no item
     * outside of it moves.
     */
    void itemsMoved(int index, const QList<int> &movedToIndexes);

    /**
     * The items starting at @p index have been replaced by changed versions of
     * themselves, for example after a rename or when their properties changed.
     */
    void itemsChanged(int index, const KFileItemList &items);

protected:
    /**
     * Creates an empty list. Only the host fills a list.
     */
    explicit WhaleItemList(QObject *parent = nullptr);

    /**
     * The modifiers for the host. Each one changes the list and emits the
     * corresponding signal. The indexes must be valid.
     */
    void resetItems(const KFileItemList &items);
    void insertItems(int index, const KFileItemList &items);
    void removeItems(int index, int count);
    void moveItems(int index, const QList<int> &movedToIndexes);
    void changeItems(int index, const KFileItemList &items);

private:
    const std::unique_ptr<WhaleItemListPrivate> d;
};

#endif
