/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEAICHANGELOG_H
#define WHALEAICHANGELOG_H

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

/**
 * @brief The lasting changes the agent made in a window during one conversation, and how to
 * undo them (AI9e).
 *
 * The tools record each thing before they first change it (a setting, the view properties of a
 * folder, the profiles), with a function that puts the old state back. "Undo the agent's changes"
 * in the AI panel runs them, newest first. The log ends with the conversation. Window size,
 * panels, split and tabs are not recorded: they last only for the session (user, 2026-11-16).
 *
 * One log per main window, its child; tools and panel find it with of().
 */
class WhaleAiChangeLog : public QObject
{
    Q_OBJECT

public:
    /** @return The log of @p window, created on the first call. */
    static WhaleAiChangeLog *of(QObject *window);

    explicit WhaleAiChangeLog(QObject *parent = nullptr);

    /**
     * Records the old state of @p key ("setting:General/ShowStatusBar", ...) unless it is in the
     * log already: undo goes back to the state before the agent's first change. @p description
     * names the thing for the transcript (translated).
     */
    void record(const QString &key, const QString &description, const std::function<void()> &undo);

    bool contains(const QString &key) const;
    int count() const;

    /** Puts everything back, newest first, and empties the log. @return The descriptions, in that order. */
    QStringList undo();

    /** Forgets everything (the conversation ended). */
    void clear();

Q_SIGNALS:
    void changed();

private:
    struct Entry {
        QString key;
        QString description;
        std::function<void()> undo;
    };
    QList<Entry> m_entries;
};

#endif
