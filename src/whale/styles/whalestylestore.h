/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALESTYLESTORE_H
#define WHALESTYLESTORE_H

#include "whalestyle.h"

#include <KSharedConfig>

#include <QList>
#include <QStringList>

/**
 * @brief The user's styles in their order, stored in "whalestylesrc" (ST1).
 *
 * Styles are settings of the user, so they live in a configuration file of their own, not in
 * the state. The file looks like this:
 *
 * @code
 * [General]
 * Version=1
 * Order=schlicht,bilder
 *
 * [Style][schlicht]
 * Name=Schlicht
 * Shortcut=Ctrl+Alt+1
 *
 * [Style][schlicht][Element][statusBar]
 * Enabled=true
 * Value=FullWidth
 * @endcode
 *
 * Changes stay in memory until save(). Elements and entries this version does not know are
 * kept and written back. A file of a newer version is read as far as it is understood and
 * keeps its version number when saved.
 */
class WhaleStyleStore
{
public:
    /** @return The version of the file format this Whale writes. */
    static int currentVersion();

    /** @return The configuration "whalestylesrc". */
    static KSharedConfigPtr defaultConfig();

    /**
     * @return An ID for a style named @p name that is not in @p takenIds: the name in lower
     *         case, letters and digits only, words joined by "-", with "-2", "-3", … if needed.
     */
    static QString idForName(const QString &name, const QStringList &takenIds);

    /** Reads the styles from @p config. */
    explicit WhaleStyleStore(KSharedConfigPtr config = defaultConfig());

    /** Reads the styles again; changes not saved are lost. */
    void load();

    /** Writes the styles and syncs the file. */
    void save();

    /** @return The version of the file as read (currentVersion() without a file). */
    int fileVersion() const;

    /** @return All styles in their order. */
    QList<WhaleStyle> styles() const;

    QStringList ids() const;

    /** @return The style with @p id, or nullptr. */
    const WhaleStyle *style(const QString &id) const;

    /**
     * Appends @p style with a new ID made from its name (its own ID is ignored).
     * @return The new ID, or an empty string if the name is empty.
     */
    QString addStyle(WhaleStyle style);

    /**
     * Replaces the style with the ID of @p style; the ID cannot change, the name must not be empty.
     * @return Whether there is such a style.
     */
    bool updateStyle(const WhaleStyle &style);

    /** @return Whether there is a style with @p id and @p name is not empty. */
    bool renameStyle(const QString &id, const QString &name);

    bool removeStyle(const QString &id);

    /** Moves the style with @p id to @p index (clamped). */
    bool moveStyle(const QString &id, int index);

    /**
     * Replaces all styles with @p styles in their order, e.g. as edited on the settings page.
     * IDs must be unique and not empty, names not empty.
     * @return Whether @p styles were taken; else nothing changed.
     */
    bool replaceStyles(const QList<WhaleStyle> &styles);

private:
    int indexOf(const QString &id) const;

    KSharedConfigPtr m_config;
    int m_fileVersion;
    QList<WhaleStyle> m_styles;
};

#endif
