/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALEVIEWPREFERENCES_H
#define WHALEVIEWPREFERENCES_H

#include <KSharedConfig>

#include "whalecontentscope.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

/**
 * @brief The view the user chose for a location ("preferred view for location", O3).
 *
 * Only the view type is remembered: the ID of a view plugin, or
 * WhaleViewHost::dolphinViewId() for the built-in file list. The file list's own view
 * mode (Icons/Compact/Details) stays in Dolphin's view properties, and the states of the
 * views stay in the history and the session (WhalePersistence).
 *
 * Locations are compared by locationKey(). There is no inheritance: a preference applies
 * to exactly its location, never to the folders below it.
 *
 * The preferences are application wide and persistent. They live in a KConfig file of
 * their own in the XDG state directory, group "PreferredViews"; each entry is keyed by a
 * hash of the location key and holds the view ID and the location key itself, so any URL
 * (Unicode, "[", "=", very long ones) is a safe key. In memory they are a hash table, so a
 * lookup does not depend on the number of entries. Another Whale process writing the file
 * is noticed by its modification time on the next lookup.
 *
 * Nothing here knows whether a view exists: a preference for a missing plugin is kept, so
 * it applies again once the plugin is back.
 *
 * F1: the content scope of the file list ("Show flat (no folders)") is remembered per location
 * the same way but separately, in the group "ContentScopes": the preferred view and the
 * preferred scope never mix. Only RecursiveFiles is written; CurrentFolder is the default
 * and removes the entry.
 */
class WhaleViewPreferences : public QObject
{
    Q_OBJECT

public:
    /**
     * @return The preferences of the application (the file "whaleviewpreferencesrc" in the
     *         XDG state directory).
     */
    static WhaleViewPreferences *instance();

    /**
     * Preferences kept in @p config, e.g. a file of a test.
     */
    explicit WhaleViewPreferences(KSharedConfigPtr config, QObject *parent = nullptr);
    ~WhaleViewPreferences() override;

    /**
     * @return The key under which a preference for @p url is kept, or an empty string if
     *         @p url is no location (empty or invalid).
     *
     * - Local files: the cleaned path (QDir::cleanPath: no trailing slash, no "//", "." or
     *   ".."), without query or fragment. Symbolic links are not resolved, so a link and its
     *   target are two locations, as the user sees them.
     * - Other URLs: path segments normalized, a trailing slash removed, an empty path is "/",
     *   no fragment, no password; the query stays (it may be part of the location, e.g. of
     *   a search).
     * The key is the fully encoded URL, so Unicode and special characters have one spelling.
     */
    static QString locationKey(const QUrl &url);

    /**
     * @return The ID of the view preferred at @p url, or an empty string if there is none.
     */
    QString preferredView(const QUrl &url);

    /**
     * Remembers @p viewId as the view of @p url and writes it to disk at once.
     */
    void setPreferredView(const QUrl &url, const QString &viewId);

    /**
     * Forgets the preference of @p url ("reset to default").
     * @return False if there was none.
     */
    bool clearPreferredView(const QUrl &url);

    /**
     * @return The number of remembered locations.
     */
    int count();

    /**
     * F1: @return The content scope of the file list preferred at @p url; CurrentFolder if
     *         there is no preference.
     */
    WhaleContentScope contentScope(const QUrl &url);

    /**
     * F1: remembers @p scope as the content scope of the file list at @p url and writes it to
     * disk at once. CurrentFolder, the default, forgets the preference.
     */
    void setContentScope(const QUrl &url, WhaleContentScope scope);

    /**
     * @return The number of locations with a remembered content scope.
     */
    int contentScopeCount();

Q_SIGNALS:
    /**
     * Is emitted when a preference of this process changed.
     */
    void changed(const QString &locationKey);

    /**
     * Is emitted when a content scope preference of this process changed.
     */
    void contentScopeChanged(const QString &locationKey);

private:
    static QString entryKey(const QString &locationKey);
    void reloadIfChanged();
    void load();
    void write();

    KSharedConfigPtr m_config;
    QHash<QString, QString> m_views; // location key -> view ID
    QHash<QString, WhaleContentScope> m_scopes; // location key -> content scope (F1)
    QString m_filePath;
    QDateTime m_fileModified;
    bool m_loaded = false;
};

#endif
