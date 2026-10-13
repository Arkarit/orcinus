/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETOOLBARLAYOUT_H
#define WHALETOOLBARLAYOUT_H

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class QDomDocument;

/**
 * @brief The entries of a toolbar as an XML GUI file defines them.
 *
 * Reads the elements KEditToolBar writes into a <ToolBar>: <Action name="…"/>,
 * <Separator/> and <Spacer/> (an expanding space). Their other attributes (such as the
 * "name" KEditToolBar gives separators and spacers) do not matter.
 *
 * And what Configure Toolbars changes about an action ("Change Icon…", "Change Text…"): the
 * <Action> elements of the file's <ActionProperties>.
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

    /**
     * The changed properties of an action: "text", "iconText", "toolTip", "icon" (the name
     * of a theme icon) and "priority" (QAction::Priority as a number), each only if it was
     * changed. Other attributes of the file, shortcuts among them, are not taken over.
     */
    using ActionProperties = QMap<QString, QString>;

    WhaleToolbarLayout() = default;
    explicit WhaleToolbarLayout(const QList<Entry> &entries, const QHash<QString, ActionProperties> &actionProperties = {});

    /**
     * @return The layout of the <ToolBar name="@p toolBarName"> in @p document; empty if
     *         there is none.
     */
    static WhaleToolbarLayout fromDocument(const QDomDocument &document, const QString &toolBarName);

    QList<Entry> entries() const;

    /**
     * @return The changed properties of the actions, by action ID.
     */
    QHash<QString, ActionProperties> actionProperties() const;

    /**
     * @return The action IDs in their order, separators as "-" and spacers as "|". For tests
     *         and diagnostics.
     */
    QStringList toStringList() const;

    bool operator==(const WhaleToolbarLayout &other) const = default;

private:
    QList<Entry> m_entries;
    QHash<QString, ActionProperties> m_actionProperties;
};

#endif
