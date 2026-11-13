/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicontext.h"
#include "whaleacptypes.h"

#include <KLocalizedString>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QUrl>

namespace
{
QString fileName(const QString &path)
{
    // A folder may come with a trailing slash.
    const QString clean = QDir::cleanPath(path);
    const QString name = QFileInfo(clean).fileName();
    return name.isEmpty() ? clean : name;
}

/** @return @p paths, at most MaxItems, with a note on the rest. */
QString itemList(const QStringList &paths)
{
    QString list = paths.mid(0, WhaleAiContextPrompt::MaxItems).join(QStringLiteral(", "));
    if (paths.size() > WhaleAiContextPrompt::MaxItems) {
        list += QStringLiteral(" … and %1 more, not listed").arg(paths.size() - WhaleAiContextPrompt::MaxItems);
    }
    return list;
}

QString subfolders(const QString &folder)
{
    // The first ones by name; a huge folder is only counted beyond a bound.
    constexpr int MaxCollected = 5000;
    QStringList names;
    int count = 0;
    QDirIterator it(folder, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        it.next();
        if (++count <= MaxCollected) {
            names.append(it.fileName());
        }
    }
    if (names.isEmpty()) {
        return QStringLiteral("none");
    }
    names.sort(Qt::CaseInsensitive);
    names = names.mid(0, WhaleAiContextPrompt::MaxSubfolders);
    QString list = names.join(QStringLiteral(", "));
    if (count > names.size()) {
        list += QStringLiteral(" … and %1 more").arg(count - names.size());
    }
    return list;
}

void appendLinks(QJsonArray &blocks, const QStringList &paths)
{
    for (const QString &path : paths.mid(0, WhaleAiContextPrompt::MaxItems)) {
        const bool isDir = QFileInfo(path).isDir();
        blocks.append(WhaleAcpContent::resourceLink(QUrl::fromLocalFile(path), fileName(path), isDir ? QStringLiteral("inode/directory") : QString()));
    }
}
}

namespace WhaleAiContextPrompt
{
QString text(const WhaleAiContext &context)
{
    if (context.isEmpty()) {
        return QString();
    }
    QStringList lines{QStringLiteral("Context from the file manager Orcinus, where the user works next to this conversation:")};
    if (!context.folder.isEmpty()) {
        lines.append(QStringLiteral("- Current folder: %1 (subfolders: %2)").arg(context.folder, subfolders(context.folder)));
    }
    if (!context.selection.isEmpty()) {
        lines.append(QStringLiteral("- Selected (%1): %2").arg(context.selection.size()).arg(itemList(context.selection)));
    }
    if (!context.otherFolder.isEmpty()) {
        lines.append(QStringLiteral("- Folder in the other split view: %1").arg(context.otherFolder));
    }
    if (!context.added.isEmpty()) {
        lines.append(QStringLiteral("- Added by the user for this message (%1): %2").arg(context.added.size()).arg(itemList(context.added)));
    }
    if (!context.reachable.isEmpty()) {
        lines.append(QStringLiteral("- Your sandbox reaches only these folders and files (besides the system, read-only): %1. "
                                    "For anything else, ask the user to give you access in Orcinus.")
                         .arg(context.reachable.join(QStringLiteral(", "))));
    }
    return lines.join(QLatin1Char('\n'));
}

QJsonArray blocks(const WhaleAiContext &context, const QString &userText)
{
    QJsonArray blocks;
    const QString contextText = text(context);
    if (!contextText.isEmpty()) {
        blocks.append(WhaleAcpContent::text(contextText));
        QStringList folders;
        if (!context.folder.isEmpty()) {
            folders.append(context.folder);
        }
        if (!context.otherFolder.isEmpty()) {
            folders.append(context.otherFolder);
        }
        appendLinks(blocks, folders);
        appendLinks(blocks, context.selection);
        appendLinks(blocks, context.added);
    }
    blocks.append(WhaleAcpContent::text(userText));
    return blocks;
}

QString summary(const WhaleAiContext &context)
{
    QStringList parts;
    if (!context.folder.isEmpty()) {
        parts.append(i18nc("@info part of a list of what was sent to an AI agent", "folder “%1”", fileName(context.folder)));
    }
    if (context.selection.size() == 1) {
        parts.append(i18nc("@info part of a list of what was sent to an AI agent", "selected “%1”", fileName(context.selection.first())));
    } else if (!context.selection.isEmpty()) {
        parts.append(i18ncp("@info part of a list of what was sent to an AI agent", "%1 selected item", "%1 selected items", context.selection.size()));
    }
    if (!context.otherFolder.isEmpty()) {
        parts.append(i18nc("@info part of a list of what was sent to an AI agent", "other view “%1”", fileName(context.otherFolder)));
    }
    if (context.added.size() == 1) {
        parts.append(i18nc("@info part of a list of what was sent to an AI agent", "“%1”", fileName(context.added.first())));
    } else if (!context.added.isEmpty()) {
        parts.append(i18ncp("@info part of a list of what was sent to an AI agent", "%1 added item", "%1 added items", context.added.size()));
    }
    if (parts.isEmpty()) {
        return QString();
    }
    return i18nc("@info %1 is a list of what was sent to an AI agent along with a message", "With: %1", parts.join(QStringLiteral(", ")));
}
}
