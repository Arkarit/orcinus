/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarlayout.h"

#include <QDomDocument>

WhaleToolbarLayout::WhaleToolbarLayout(const QList<Entry> &entries)
    : m_entries(entries)
{
}

WhaleToolbarLayout WhaleToolbarLayout::fromDocument(const QDomDocument &document, const QString &toolBarName)
{
    const QDomNodeList toolBars = document.elementsByTagName(QStringLiteral("ToolBar"));
    for (int i = 0; i < toolBars.count(); ++i) {
        const QDomElement toolBar = toolBars.item(i).toElement();
        if (toolBar.attribute(QStringLiteral("name")) != toolBarName) {
            continue;
        }
        QList<Entry> entries;
        for (QDomElement element = toolBar.firstChildElement(); !element.isNull(); element = element.nextSiblingElement()) {
            const QString tag = element.tagName();
            if (tag == QLatin1String("Action")) {
                const QString id = element.attribute(QStringLiteral("name"));
                if (!id.isEmpty()) {
                    entries.append({Entry::Type::Action, id});
                }
            } else if (tag == QLatin1String("Separator")) {
                entries.append({Entry::Type::Separator, QString()});
            } else if (tag == QLatin1String("Spacer")) {
                entries.append({Entry::Type::Spacer, QString()});
            }
            // <text> is the toolbar's name; <ActionList> and <Merge> do not occur here.
        }
        return WhaleToolbarLayout(entries);
    }
    return {};
}

QList<WhaleToolbarLayout::Entry> WhaleToolbarLayout::entries() const
{
    return m_entries;
}

QStringList WhaleToolbarLayout::toStringList() const
{
    QStringList result;
    for (const Entry &entry : m_entries) {
        switch (entry.type) {
        case Entry::Type::Action:
            result.append(entry.actionId);
            break;
        case Entry::Type::Separator:
            result.append(QStringLiteral("-"));
            break;
        case Entry::Type::Spacer:
            result.append(QStringLiteral("|"));
            break;
        }
    }
    return result;
}
