/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALESELECTION_H
#define WHALESELECTION_H

#include "whaleviewapi_export.h"

#include <QList>
#include <QMetaType>
#include <QUrl>

/**
 * @brief The selection of a view area, by URL.
 *
 * - selected(): the items that are selected, in the host's order, without duplicates.
 * - current(): the current item, which has the keyboard focus inside the view. It is
 *   independent of the selection: as in Dolphin, there usually is a current item
 *   (the first one by default) even when nothing is selected, and it need not be
 *   selected itself. An empty URL means there is no current item.
 *
 * A selection only refers to items of the current location's listing (WhaleItemList);
 * the host ignores other URLs.
 */
class WHALEVIEWAPI_EXPORT WhaleSelection
{
public:
    WhaleSelection();
    explicit WhaleSelection(const QList<QUrl> &selected, const QUrl &current = QUrl());

    QList<QUrl> selected() const;
    QUrl current() const;

    /**
     * @return True if no item is selected. The current item does not count.
     */
    bool isEmpty() const;
    bool contains(const QUrl &url) const;

    bool operator==(const WhaleSelection &other) const;
    bool operator!=(const WhaleSelection &other) const;

private:
    QList<QUrl> m_selected;
    QUrl m_current;
};

WHALEVIEWAPI_EXPORT QDebug operator<<(QDebug debug, const WhaleSelection &selection);

Q_DECLARE_METATYPE(WhaleSelection)

#endif
