/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETOOLBARMIGRATION_H
#define WHALETOOLBARMIGRATION_H

#include <QByteArray>
#include <QString>

class WhaleToolbarSettings;

/**
 * @brief The one-time migration of the toolbars to the location bar of each file view (TB7).
 *
 * Before TB7 the location bars were part of the main toolbar or above the views; now each
 * view has its own, in its File View Toolbar. Two kinds of users would lose it:
 *
 * - With a File View Toolbar layout of their own (kxmlgui5/orcinus/whalefileviewtoolbarui.rc
 *   without "whale_location_bar"): the location bar is inserted into that file, once. The
 *   original is kept as "<file>.pre-tb7" (only the first time; it is never overwritten).
 *   Nothing else in the file changes, byte for byte.
 * - With the File View Toolbars hidden (TB5): they are shown again, once. Hiding them later
 *   hides the location bars, as the user then chose.
 *
 * Each part has a marker of its own in the group WhaleToolbarSettings::groupName(); a part is
 * marked only when it succeeded (or there was nothing to do), so a failed part is tried again
 * on the next start, and a part that is done is never done again. The user's dolphinui.rc is
 * not touched: an "url_navigators" in it is passed over by KXmlGui and kept for older Whales.
 */
namespace WhaleToolbarMigration
{
/**
 * Runs what is not done yet, for the application's settings and the user's File View
 * Toolbar file. Problems are logged; the next call tries again.
 */
void run();

/**
 * run() with other @p settings and @p file (tests).
 */
void run(WhaleToolbarSettings *settings, const QString &file);

/**
 * @return The user's File View Toolbar file (it may not exist).
 */
QString userFileViewFile();

/**
 * @return The keys of the markers in WhaleToolbarSettings::groupName().
 */
QString layoutMarker();
QString visibilityMarker();

/**
 * @return The suffix of the copy of the user's file as it was before the migration.
 */
QString backupSuffix();

enum class Result {
    Unchanged, ///< nothing to do: no such file, no File View Toolbar in it, or it has the location bar
    Migrated, ///< saved with the location bar, the original kept as backup
    Failed, ///< nothing changed in the file; @p error says why
};

/**
 * Inserts the location bar into the File View Toolbar of the XML GUI file @p file, keeping a
 * backup first.
 */
Result migrateFileViewFile(const QString &file, QString *error);

/**
 * @return @p content with "whale_location_bar" in the File View Toolbar: after the last of
 *         Back, Forward, Up and Reload, or else before its first entry. Everything else stays
 *         as it is, byte for byte. @p content itself if the toolbar has the location bar or
 *         there is no such toolbar; an empty array and @p error if it cannot be read.
 */
QByteArray withLocationBar(const QByteArray &content, QString *error);
}

#endif
