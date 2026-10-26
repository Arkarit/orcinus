/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whaleitemlist.h"

class WhaleItemListPrivate
{
public:
    KFileItemList items;
};

WhaleItemList::WhaleItemList(QObject *parent)
    : QObject(parent)
    , d(new WhaleItemListPrivate)
{
}

WhaleItemList::~WhaleItemList() = default;

int WhaleItemList::count() const
{
    return d->items.count();
}

bool WhaleItemList::isEmpty() const
{
    return d->items.isEmpty();
}

KFileItem WhaleItemList::item(int index) const
{
    return d->items.value(index);
}

KFileItemList WhaleItemList::items() const
{
    return d->items;
}

void WhaleItemList::resetItems(const KFileItemList &items)
{
    d->items = items;
    Q_EMIT itemsReset();
}

void WhaleItemList::insertItems(int index, const KFileItemList &items)
{
    Q_ASSERT(index >= 0 && index <= d->items.count());
    d->items.insert(index, items.count(), KFileItem());
    std::copy(items.begin(), items.end(), d->items.begin() + index);
    Q_EMIT itemsInserted(index, items);
}

void WhaleItemList::removeItems(int index, int count)
{
    Q_ASSERT(index >= 0 && count >= 0 && index + count <= d->items.count());
    const KFileItemList removed = d->items.mid(index, count);
    d->items.remove(index, count);
    Q_EMIT itemsRemoved(index, removed);
}

void WhaleItemList::moveItems(int index, const QList<int> &movedToIndexes)
{
    Q_ASSERT(index >= 0 && index + movedToIndexes.count() <= d->items.count());
    const KFileItemList old = d->items.mid(index, movedToIndexes.count());
    for (int i = 0; i < old.count(); ++i) {
        d->items[movedToIndexes.at(i)] = old.at(i);
    }
    Q_EMIT itemsMoved(index, movedToIndexes);
}

void WhaleItemList::changeItems(int index, const KFileItemList &items)
{
    Q_ASSERT(index >= 0 && index + items.count() <= d->items.count());
    std::copy(items.begin(), items.end(), d->items.begin() + index);
    Q_EMIT itemsChanged(index, items);
}

#include "moc_whaleitemlist.cpp"
