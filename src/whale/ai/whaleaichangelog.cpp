/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaichangelog.h"

#include <algorithm>

WhaleAiChangeLog *WhaleAiChangeLog::of(QObject *window)
{
    if (!window) {
        return nullptr;
    }
    auto *log = window->findChild<WhaleAiChangeLog *>(QString(), Qt::FindDirectChildrenOnly);
    return log ? log : new WhaleAiChangeLog(window);
}

WhaleAiChangeLog::WhaleAiChangeLog(QObject *parent)
    : QObject(parent)
{
}

void WhaleAiChangeLog::record(const QString &key, const QString &description, const std::function<void()> &undo)
{
    if (contains(key)) {
        return;
    }
    m_entries.append({key, description, undo});
    Q_EMIT changed();
}

bool WhaleAiChangeLog::contains(const QString &key) const
{
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [&key](const Entry &entry) {
        return entry.key == key;
    });
}

int WhaleAiChangeLog::count() const
{
    return m_entries.size();
}

QStringList WhaleAiChangeLog::undo()
{
    // Taken out first: an undo function may lead to new records (it must not, but the log stays sane).
    const QList<Entry> entries = std::exchange(m_entries, {});
    QStringList descriptions;
    for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
        if (it->undo) {
            it->undo();
        }
        descriptions.append(it->description);
    }
    if (!entries.isEmpty()) {
        Q_EMIT changed();
    }
    return descriptions;
}

void WhaleAiChangeLog::clear()
{
    if (m_entries.isEmpty()) {
        return;
    }
    m_entries.clear();
    Q_EMIT changed();
}

#include "moc_whaleaichangelog.cpp"
