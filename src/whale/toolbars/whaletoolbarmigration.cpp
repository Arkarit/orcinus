/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletoolbarmigration.h"

#include "whalefileviewtoolbarclient.h"
#include "whalelocationbar.h"
#include "whaletoolbarsettings.h"
#include "whaleviewdebug.h"

#include <KConfigGroup>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QXmlStreamReader>

#include <optional>

namespace
{
// The whitespace in front of the element at @p position, if it starts its line.
std::optional<QString> indentation(const QString &text, qsizetype position)
{
    const qsizetype lineStart = position > 0 ? text.lastIndexOf(QLatin1Char('\n'), position - 1) + 1 : 0;
    const QString indent = text.mid(lineStart, position - lineStart);
    if (!indent.trimmed().isEmpty()) {
        return std::nullopt;
    }
    return indent;
}

// QXmlStreamReader::characterOffset() is at or a little after the end of the token read (it
// may have read the '<' of the next one): the end of the tag at or before @p offset ...
qsizetype tagEnd(const QString &text, qsizetype offset)
{
    return text.lastIndexOf(QLatin1Char('>'), offset - 1) + 1;
}

// ... and its start ('<' is not allowed in attribute values).
qsizetype tagStart(const QString &text, qsizetype offset)
{
    return text.lastIndexOf(QLatin1Char('<'), tagEnd(text, offset) - 1);
}
}

namespace WhaleToolbarMigration
{
QString userFileViewFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kxmlgui5/") + QCoreApplication::applicationName()
        + QLatin1Char('/') + WhaleFileViewToolbarClient::rcFile();
}

QString layoutMarker()
{
    return QStringLiteral("MigratedFileViewLayoutTB7");
}

QString visibilityMarker()
{
    return QStringLiteral("MigratedFileViewVisibilityTB7");
}

QString backupSuffix()
{
    return QStringLiteral(".pre-tb7");
}

QByteArray withLocationBar(const QByteArray &content, QString *error)
{
    // Edited as text: the rest of the file stays as the user (or KDE) wrote it.
    QString text = QString::fromUtf8(content);
    if (text.toUtf8() != content) {
        *error = QStringLiteral("the file is not UTF-8");
        return {};
    }
    // Back, Forward, Up and Reload: the location bar comes after them, as in the default.
    const QStringList navigation = {QStringLiteral("go_back"), QStringLiteral("go_forward"), QStringLiteral("go_up"), QStringLiteral("view_redisplay")};

    QXmlStreamReader reader(text);
    int depth = 0;
    int toolBarDepth = -1; // while in the File View Toolbar
    bool toolBarFound = false;
    bool hasLocationBar = false;
    QString childTag;
    QString childAction;
    qsizetype childStart = -1;
    qsizetype afterNavigation = -1; // the end of the last of Back … Reload
    qsizetype navigationStart = -1;
    qsizetype firstEntry = -1; // the start of the first entry (not <text>)
    qsizetype afterText = -1;
    qsizetype toolBarEnd = -1; // the start of </ToolBar>
    bool emptyToolBar = false;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            ++depth;
            if (!toolBarFound && reader.name() == QLatin1String("ToolBar")
                && reader.attributes().value(QLatin1String("name")) == WhaleFileViewToolbarClient::toolBarName()) {
                toolBarFound = true;
                toolBarDepth = depth;
            } else if (toolBarDepth >= 0 && depth == toolBarDepth + 1) {
                childTag = reader.name().toString();
                childAction = reader.attributes().value(QLatin1String("name")).toString();
                childStart = tagStart(text, reader.characterOffset());
                if (childTag != QLatin1String("text") && firstEntry < 0) {
                    firstEntry = childStart;
                }
                if (childTag == QLatin1String("Action") && childAction == WhaleLocationBar::actionName()) {
                    hasLocationBar = true;
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (toolBarDepth >= 0 && depth == toolBarDepth + 1) {
                if (childTag == QLatin1String("Action") && navigation.contains(childAction)) {
                    afterNavigation = tagEnd(text, reader.characterOffset());
                    navigationStart = childStart;
                } else if (childTag == QLatin1String("text")) {
                    afterText = tagEnd(text, reader.characterOffset());
                }
            } else if (toolBarDepth >= 0 && depth == toolBarDepth) {
                toolBarEnd = tagStart(text, reader.characterOffset());
                emptyToolBar = text.at(tagEnd(text, reader.characterOffset()) - 2) == QLatin1Char('/');
                toolBarDepth = -1;
            }
            --depth;
        }
    }
    if (reader.hasError()) {
        *error = QStringLiteral("line %1: %2").arg(reader.lineNumber()).arg(reader.errorString());
        return {};
    }
    if (!toolBarFound || hasLocationBar || emptyToolBar) {
        // No File View Toolbar of the user's (KDE takes the installed one), the location bar is
        // there, or <ToolBar/> without anything: the user's choice.
        return content;
    }

    const QString element = QStringLiteral("<Action name=\"%1\"/>").arg(WhaleLocationBar::actionName());
    if (afterNavigation >= 0) {
        const std::optional<QString> indent = indentation(text, navigationStart);
        text.insert(afterNavigation, indent ? QLatin1Char('\n') + *indent + element : element);
    } else if (firstEntry >= 0) {
        const std::optional<QString> indent = indentation(text, firstEntry);
        text.insert(firstEntry, indent ? element + QLatin1Char('\n') + *indent : element);
    } else if (afterText >= 0) {
        text.insert(afterText, element);
    } else {
        text.insert(toolBarEnd, element);
    }
    return text.toUtf8();
}

Result migrateFileViewFile(const QString &file, QString *error)
{
    QFile original(file);
    if (!original.exists()) {
        return Result::Unchanged;
    }
    if (!original.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1: %2").arg(file, original.errorString());
        return Result::Failed;
    }
    const QByteArray content = original.readAll();
    original.close();
    const QByteArray migrated = withLocationBar(content, error);
    if (migrated.isEmpty()) {
        *error = QStringLiteral("cannot read %1: %2").arg(file, *error);
        return Result::Failed;
    }
    if (migrated == content) {
        return Result::Unchanged;
    }

    // The backup of the file as it was before TB7: made once, never overwritten (a backup of
    // an earlier attempt is that file, too).
    const QString backup = file + backupSuffix();
    const QFileInfo backupInfo(backup);
    if (!backupInfo.exists()) {
        if (!QFile::copy(file, backup)) {
            *error = QStringLiteral("cannot back %1 up as %2").arg(file, backup);
            return Result::Failed;
        }
        QFile copy(backup);
        if (!copy.open(QIODevice::ReadOnly) || copy.readAll() != content) {
            copy.close();
            QFile::remove(backup);
            *error = QStringLiteral("the backup %1 differs from %2").arg(backup, file);
            return Result::Failed;
        }
    } else if (!backupInfo.isFile()) {
        *error = QStringLiteral("cannot back %1 up: %2 is not a file").arg(file, backup);
        return Result::Failed;
    }

    // Replaced as a whole or not at all.
    QSaveFile saved(file);
    if (!saved.open(QIODevice::WriteOnly) || saved.write(migrated) != migrated.size() || !saved.commit()) {
        *error = QStringLiteral("cannot write %1: %2").arg(file, saved.errorString());
        return Result::Failed;
    }
    return Result::Migrated;
}

void run(WhaleToolbarSettings *settings, const QString &file)
{
    KConfigGroup group = settings->config()->group(WhaleToolbarSettings::groupName());
    bool changed = false;
    if (!group.readEntry(layoutMarker(), false)) {
        QString error;
        switch (migrateFileViewFile(file, &error)) {
        case Result::Failed:
            qCWarning(WhaleViewDebug) << "Could not add the location bar to the File View Toolbar; trying again on the next start:" << error;
            break;
        case Result::Migrated:
            qCInfo(WhaleViewDebug) << "Added the location bar to the File View Toolbar of" << file << "- the previous file is" << file + backupSuffix();
            Q_FALLTHROUGH();
        case Result::Unchanged:
            group.writeEntry(layoutMarker(), true);
            changed = true;
            break;
        }
    }
    if (!group.readEntry(visibilityMarker(), false)) {
        // Hidden File View Toolbars meant a window with location bars before TB7, and they
        // hold the location bars now. Once; after that it is the user's setting again.
        if (!settings->fileViewToolBarsVisible()) {
            settings->setFileViewToolBarsVisible(true);
            qCInfo(WhaleViewDebug) << "Showing the File View Toolbars again: they hold the location bars now";
        }
        group.writeEntry(visibilityMarker(), true);
        changed = true;
    }
    if (changed && !settings->config()->sync()) {
        qCWarning(WhaleViewDebug) << "Could not save the state of the toolbar migration";
    }
}

void run()
{
    run(WhaleToolbarSettings::self(), userFileViewFile());
}
}
