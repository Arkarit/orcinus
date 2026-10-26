/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALECONTENTSCOPE_H
#define WHALECONTENTSCOPE_H

#include <QString>

#include <optional>

/**
 * @brief Which items the built-in file list (Icons, Compact, Details) shows (F1).
 *
 * The scope is a property of the file list's content, not a view of its own: the view
 * mode stays what it is. Plugin views (e.g. the treemap) ignore it.
 */
enum class WhaleContentScope {
    /// The items of the location, as in Dolphin (the default).
    CurrentFolder,
    /// The files below the location, in all its folders, without folders ("Show flat (no folders)").
    RecursiveFiles,
};

namespace WhaleContentScopes
{
/// The name of @p scope in persistent data and development tools.
inline QString name(WhaleContentScope scope)
{
    return scope == WhaleContentScope::RecursiveFiles ? QStringLiteral("recursive-files") : QStringLiteral("current-folder");
}

/// The scope named @p name (see name()), or nothing for an unknown name.
inline std::optional<WhaleContentScope> fromName(const QString &name)
{
    if (name == QLatin1String("recursive-files")) {
        return WhaleContentScope::RecursiveFiles;
    }
    if (name == QLatin1String("current-folder")) {
        return WhaleContentScope::CurrentFolder;
    }
    return std::nullopt;
}
}

#endif
