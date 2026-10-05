/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whaleselection.h"

#include <QDebug>

WhaleSelection::WhaleSelection() = default;

WhaleSelection::WhaleSelection(const QList<QUrl> &selected, const QUrl &current)
    : m_current(current)
{
    for (const QUrl &url : selected) {
        if (!m_selected.contains(url)) {
            m_selected.append(url);
        }
    }
}

QList<QUrl> WhaleSelection::selected() const
{
    return m_selected;
}

QUrl WhaleSelection::current() const
{
    return m_current;
}

bool WhaleSelection::isEmpty() const
{
    return m_selected.isEmpty();
}

bool WhaleSelection::contains(const QUrl &url) const
{
    return m_selected.contains(url);
}

bool WhaleSelection::operator==(const WhaleSelection &other) const
{
    return m_current == other.m_current && m_selected == other.m_selected;
}

bool WhaleSelection::operator!=(const WhaleSelection &other) const
{
    return !(*this == other);
}

QDebug operator<<(QDebug debug, const WhaleSelection &selection)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "WhaleSelection(" << selection.selected() << ", current " << selection.current() << ')';
    return debug;
}
