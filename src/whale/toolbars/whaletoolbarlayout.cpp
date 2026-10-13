/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarlayout.h"

#include <QDomDocument>

namespace
{
QHash<QString, WhaleToolbarLayout::ActionProperties> actionPropertiesOf(const QDomDocument &document)
{
    static const QStringList taken = {QStringLiteral("text"),
                                      QStringLiteral("iconText"),
                                      QStringLiteral("toolTip"),
                                      QStringLiteral("icon"),
                                      QStringLiteral("priority")};
    QHash<QString, WhaleToolbarLayout::ActionProperties> result;
    // XML GUI compares these two tag names without case.
    for (QDomElement properties = document.documentElement().firstChildElement(); !properties.isNull(); properties = properties.nextSiblingElement()) {
        if (properties.tagName().compare(QLatin1String("ActionProperties"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        for (QDomElement action = properties.firstChildElement(); !action.isNull(); action = action.nextSiblingElement()) {
            const QString id = action.attribute(QStringLiteral("name"));
            if (id.isEmpty() || action.tagName().compare(QLatin1String("Action"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            for (const QString &property : taken) {
                if (action.hasAttribute(property)) {
                    result[id].insert(property, action.attribute(property));
                }
            }
        }
    }
    return result;
}
}

WhaleToolbarLayout::WhaleToolbarLayout(const QList<Entry> &entries, const QHash<QString, ActionProperties> &actionProperties)
    : m_entries(entries)
    , m_actionProperties(actionProperties)
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
        return WhaleToolbarLayout(entries, actionPropertiesOf(document));
    }
    return {};
}

QList<WhaleToolbarLayout::Entry> WhaleToolbarLayout::entries() const
{
    return m_entries;
}

QHash<QString, WhaleToolbarLayout::ActionProperties> WhaleToolbarLayout::actionProperties() const
{
    return m_actionProperties;
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
