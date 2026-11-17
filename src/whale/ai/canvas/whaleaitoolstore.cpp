/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaitoolstore.h"

#include "whaleaitoolbackend.h"

#include <KConfigGroup>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
constexpr qint64 MaxManifest = 64 * 1024;
constexpr int MaxName = 100;

const QString ManifestName = QStringLiteral("tool.json");
}

QString WhaleAiTool::entryPath() const
{
    return QDir(folder).filePath(entry);
}

WhaleAiToolStore::WhaleAiToolStore()
    : WhaleAiToolStore(defaultRoot(), KSharedConfig::openConfig())
{
}

WhaleAiToolStore::WhaleAiToolStore(const QString &root, KSharedConfigPtr config)
    : m_root(root)
    , m_config(std::move(config))
{
}

QString WhaleAiToolStore::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/orcinus/ai/tools");
}

QString WhaleAiToolStore::root() const
{
    return m_root;
}

bool WhaleAiToolStore::isValidId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,63}$"));
    return pattern.match(id).hasMatch();
}

QStringList WhaleAiToolStore::ids() const
{
    QStringList result;
    const QStringList folders = QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : folders) {
        if (tool(folder)) {
            result.append(folder);
        }
    }
    return result;
}

std::optional<WhaleAiTool> WhaleAiToolStore::tool(const QString &id, QString *error) const
{
    auto fail = [error](const QString &message) -> std::optional<WhaleAiTool> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (!isValidId(id)) {
        return fail(QStringLiteral("A tool id (its folder name) has only lower case letters, digits, '-' and '_'."));
    }
    WhaleAiTool tool;
    tool.id = id;
    tool.folder = QDir(m_root).filePath(id);
    QFile file(QDir(tool.folder).filePath(ManifestName));
    if (!file.exists()) {
        return fail(QStringLiteral("%1 has no tool.json.").arg(tool.folder));
    }
    if (file.size() > MaxManifest || !file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("tool.json cannot be read or is too large."));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        return fail(QStringLiteral("tool.json is not a JSON object: %1.").arg(parseError.errorString()));
    }
    const QJsonObject json = document.object();
    tool.name = json.value(QLatin1String("name")).toString().trimmed();
    if (tool.name.isEmpty() || tool.name.size() > MaxName) {
        return fail(QStringLiteral("tool.json needs a \"name\" of 1 to %1 characters.").arg(MaxName));
    }
    tool.icon = json.value(QLatin1String("icon")).toString();
    tool.description = json.value(QLatin1String("description")).toString();
    tool.entry = json.value(QLatin1String("entry")).toString(QStringLiteral("main.qml"));
    const QString entryPath = QDir::cleanPath(tool.entryPath());
    if (QDir::isAbsolutePath(tool.entry) || !entryPath.startsWith(QDir::cleanPath(tool.folder) + QLatin1Char('/'))
        || !entryPath.endsWith(QLatin1String(".qml"))) {
        return fail(QStringLiteral("\"entry\" must be a .qml file inside the tool's folder."));
    }
    if (!QFile::exists(entryPath)) {
        return fail(QStringLiteral("The entry %1 does not exist.").arg(tool.entry));
    }
    for (const QJsonValue &value : json.value(QLatin1String("writes")).toArray()) {
        tool.writes.append(value.toString());
    }
    tool.settings = json.value(QLatin1String("settings")).toObject();
    if (json.contains(QLatin1String("backend"))) {
        QString backendError;
        if (!WhaleAiToolBackend::parseCommand(json.value(QLatin1String("backend")), &backendError)) {
            return fail(backendError);
        }
        tool.backend = json.value(QLatin1String("backend")).toObject();
    }
    return tool;
}

QStringList WhaleAiToolStore::files(const QString &id) const
{
    QStringList result;
    if (!isValidId(id)) {
        return result;
    }
    const QDir folder(QDir(m_root).filePath(id));
    QDirIterator it(folder.path(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = folder.relativeFilePath(path);
        // Hidden files and folders (caches, editor files) are not part of the tool.
        if (relative.startsWith(QLatin1Char('.')) || relative.contains(QLatin1String("/."))) {
            continue;
        }
        result.append(relative);
    }
    result.sort();
    return result;
}

QString WhaleAiToolStore::fingerprint(const QString &id) const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QDir folder(QDir(m_root).filePath(id));
    for (const QString &relative : files(id)) {
        hash.addData(relative.toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        QFile file(folder.filePath(relative));
        if (file.open(QIODevice::ReadOnly)) {
            hash.addData(&file);
        }
        hash.addData(QByteArrayView("\0", 1));
    }
    return QString::fromLatin1(hash.result().toHex());
}

KConfigGroup WhaleAiToolStore::group() const
{
    return KConfigGroup(m_config, QStringLiteral("AI Tools"));
}

QStringList WhaleAiToolStore::pinned() const
{
    return group().readEntry("Pinned", QStringList());
}

void WhaleAiToolStore::pin(const QString &id)
{
    KConfigGroup config = group();
    QStringList ids = pinned();
    if (!ids.contains(id)) {
        ids.append(id);
    }
    config.writeEntry("Pinned", ids);
    config.writeEntry(QStringLiteral("Trusted_%1").arg(id), fingerprint(id));
    config.sync();
}

void WhaleAiToolStore::unpin(const QString &id)
{
    KConfigGroup config = group();
    QStringList ids = pinned();
    ids.removeAll(id);
    config.writeEntry("Pinned", ids);
    config.deleteEntry(QStringLiteral("Trusted_%1").arg(id));
    config.sync();
}

bool WhaleAiToolStore::isTrusted(const QString &id) const
{
    const QString trusted = group().readEntry(QStringLiteral("Trusted_%1").arg(id), QString());
    return !trusted.isEmpty() && pinned().contains(id) && trusted == fingerprint(id);
}

KConfigGroup WhaleAiToolStore::settingsGroup(const QString &id) const
{
    return KConfigGroup(m_config, QStringLiteral("AI Tool %1").arg(id));
}
