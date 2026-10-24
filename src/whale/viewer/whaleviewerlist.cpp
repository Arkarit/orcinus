/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleviewerlist.h"

#include <QImageReader>
#include <QMimeType>
#include <QSet>

#include <algorithm>

namespace
{
bool matches(const QString &name, const QString &pattern)
{
    if (pattern.endsWith(QLatin1String("/*"))) {
        return name.startsWith(QStringView(pattern).chopped(1));
    }
    return name == pattern;
}

bool decodable(const QMimeType &type)
{
    static const QSet<QString> supported = [] {
        QSet<QString> names;
        const QList<QByteArray> types = QImageReader::supportedMimeTypes();
        for (const QByteArray &name : types) {
            names.insert(QString::fromLatin1(name));
        }
        return names;
    }();
    if (supported.contains(type.name())) {
        return true;
    }
    const QStringList aliases = type.aliases();
    for (const QString &alias : aliases) {
        if (supported.contains(alias)) {
            return true;
        }
    }
    return false;
}

QUrl folderOf(const QUrl &url)
{
    return url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
}
}

bool WhaleViewerList::isDisplayable(const KFileItem &item, const QStringList &mimeTypes)
{
    if (item.isNull() || item.isDir() || item.localPath().isEmpty()) {
        return false;
    }
    const QMimeType type = item.determineMimeType();
    QStringList names = type.allAncestors();
    names.prepend(type.name());
    const bool wanted = std::any_of(mimeTypes.cbegin(), mimeTypes.cend(), [&names](const QString &pattern) {
        return std::any_of(names.cbegin(), names.cend(), [&pattern](const QString &name) {
            return matches(name, pattern);
        });
    });
    return wanted && decodable(type);
}

WhaleViewerList WhaleViewerList::fromFolder(const KFileItemList &viewItems, const KFileItem &opened, const QStringList &mimeTypes)
{
    WhaleViewerList list;
    const QUrl folder = folderOf(opened.url());
    for (const KFileItem &item : viewItems) {
        if (item.url() == opened.url()) {
            list.m_current = list.m_items.count();
            list.m_items.append(item);
        } else if (folderOf(item.url()) == folder && isDisplayable(item, mimeTypes)) {
            list.m_items.append(item);
        }
    }
    if (list.m_current < 0) {
        list.m_items = {opened};
        list.m_current = 0;
    }
    return list;
}

WhaleViewerList WhaleViewerList::fromSelection(const KFileItemList &items, const QUrl &current, const QStringList &mimeTypes)
{
    WhaleViewerList list;
    list.m_selection = true;
    for (const KFileItem &item : items) {
        if (isDisplayable(item, mimeTypes)) {
            if (item.url() == current) {
                list.m_current = list.m_items.count();
            }
            list.m_items.append(item);
        }
    }
    if (list.m_current < 0 && !list.m_items.isEmpty()) {
        list.m_current = 0;
    }
    return list;
}

bool WhaleViewerList::isEmpty() const
{
    return m_items.isEmpty();
}

int WhaleViewerList::count() const
{
    return m_items.count();
}

KFileItemList WhaleViewerList::items() const
{
    return m_items;
}

bool WhaleViewerList::isSelection() const
{
    return m_selection;
}

int WhaleViewerList::currentIndex() const
{
    return m_current;
}

KFileItem WhaleViewerList::currentItem() const
{
    return m_current >= 0 ? m_items.at(m_current) : KFileItem();
}

void WhaleViewerList::setCurrentIndex(int index)
{
    if (index >= 0 && index < m_items.count()) {
        m_current = index;
    }
}

bool WhaleViewerList::move(int steps, bool wrapAround)
{
    if (m_items.isEmpty()) {
        return false;
    }
    const int count = m_items.count();
    const int target = m_current + steps;
    if (wrapAround) {
        m_current = ((target % count) + count) % count;
        return false;
    }
    m_current = std::clamp(target, 0, count - 1);
    return m_current != target;
}
