/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETOOLBARLAYOUT_H
#define WHALETOOLBARLAYOUT_H

#include <QList>
#include <QString>
#include <QStringList>

class QDomDocument;

/**
 * @brief The entries of a toolbar as an XML GUI file defines them.
 *
 * Reads the elements KEditToolBar writes into a <ToolBar>: <Action name="…"/>,
 * <Separator/> and <Spacer/> (an expanding space). Their other attributes (such as the
 * "name" KEditToolBar gives separators and spacers) do not matter.
 */
class WhaleToolbarLayout
{
public:
    struct Entry {
        enum class Type {
            Action,
            Separator,
            Spacer,
        };
        Type type = Type::Action;
        QString actionId; ///< for Type::Action

        bool operator==(const Entry &other) const = default;
    };

    WhaleToolbarLayout() = default;
    explicit WhaleToolbarLayout(const QList<Entry> &entries);

    /**
     * @return The layout of the <ToolBar name="@p toolBarName"> in @p document; empty if
     *         there is none.
     */
    static WhaleToolbarLayout fromDocument(const QDomDocument &document, const QString &toolBarName);

    QList<Entry> entries() const;

    /**
     * @return The action IDs in their order, separators as "-" and spacers as "|". For tests
     *         and diagnostics.
     */
    QStringList toStringList() const;

    bool operator==(const WhaleToolbarLayout &other) const = default;

private:
    QList<Entry> m_entries;
};

#endif
