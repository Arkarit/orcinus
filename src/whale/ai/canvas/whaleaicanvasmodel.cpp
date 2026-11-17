/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaleaicanvasmodel.h"

#include <KDirWatch>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMimeDatabase>

namespace
{
constexpr int MaxTitle = 200;
constexpr int MaxCaption = 2000;

struct TypeName {
    WhaleAiCanvasBlock::Type type;
    QLatin1String name;
};

constexpr TypeName typeNames[] = {
    {WhaleAiCanvasBlock::Type::Image, QLatin1String("image")},
    {WhaleAiCanvasBlock::Type::Video, QLatin1String("video")},
    {WhaleAiCanvasBlock::Type::Audio, QLatin1String("audio")},
    {WhaleAiCanvasBlock::Type::Text, QLatin1String("text")},
    {WhaleAiCanvasBlock::Type::Files, QLatin1String("files")},
    {WhaleAiCanvasBlock::Type::Compare, QLatin1String("compare")},
    {WhaleAiCanvasBlock::Type::Watch, QLatin1String("watch")},
    {WhaleAiCanvasBlock::Type::Stream, QLatin1String("stream")},
};

/** @return The absolute local path of @p text (a path or a file:// URL), or empty. */
QString localPath(const QString &text)
{
    const QString path = text.startsWith(QLatin1String("file:")) ? QUrl(text).toLocalFile() : text;
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        return QString();
    }
    return QDir::cleanPath(path);
}

bool isMedia(const QMimeDatabase &db, const QFileInfo &info)
{
    const QString name = db.mimeTypeForFile(info, QMimeDatabase::MatchExtension).name();
    return name.startsWith(QLatin1String("image/")) || name.startsWith(QLatin1String("video/"));
}
}

QString WhaleAiCanvasBlock::typeName(Type type)
{
    for (const TypeName &entry : typeNames) {
        if (entry.type == type) {
            return entry.name;
        }
    }
    return QString();
}

QJsonObject WhaleAiCanvasBlock::toJson() const
{
    QJsonObject json{{QStringLiteral("type"), typeName(type)}};
    if (!title.isEmpty()) {
        json.insert(QStringLiteral("title"), title);
    }
    if (!text.isEmpty()) {
        json.insert(QStringLiteral("text"), text);
    }
    if (type == Type::Stream) {
        json.insert(QStringLiteral("url"), url.toString());
    } else if (type == Type::Compare && paths.size() == 2) {
        json.insert(QStringLiteral("before"), paths.at(0));
        json.insert(QStringLiteral("after"), paths.at(1));
    } else if (paths.size() == 1 && type != Type::Files) {
        json.insert(QStringLiteral("path"), paths.constFirst());
    } else if (!paths.isEmpty()) {
        json.insert(QStringLiteral("paths"), QJsonArray::fromStringList(paths));
    }
    return json;
}

WhaleAiCanvasModel::WhaleAiCanvasModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

WhaleAiCanvasModel::~WhaleAiCanvasModel() = default;

std::optional<WhaleAiCanvasBlock> WhaleAiCanvasModel::parse(const QJsonObject &json, QString *error)
{
    using Type = WhaleAiCanvasBlock::Type;
    WhaleAiCanvasBlock block;
    const QString typeText = json.value(QLatin1String("type")).toString();
    bool known = false;
    for (const TypeName &entry : typeNames) {
        if (entry.name == typeText) {
            block.type = entry.type;
            known = true;
        }
    }
    if (!known) {
        *error = QStringLiteral("\"type\" must be one of image, video, audio, text, files, compare, watch, stream.");
        return std::nullopt;
    }
    block.title = json.value(QLatin1String("title")).toString();
    if (block.title.size() > MaxTitle) {
        *error = QStringLiteral("\"title\" is longer than %1 characters.").arg(MaxTitle);
        return std::nullopt;
    }
    block.text = json.value(QLatin1String("text")).toString();
    if (block.type == Type::Text) {
        if (block.text.trimmed().isEmpty()) {
            *error = QStringLiteral("A text block needs \"text\" (Markdown).");
            return std::nullopt;
        }
        if (block.text.size() > MaxText) {
            *error = QStringLiteral("\"text\" is longer than %1 characters.").arg(MaxText);
            return std::nullopt;
        }
        return block;
    }
    if (block.text.size() > MaxCaption) {
        *error = QStringLiteral("\"text\" (the caption) is longer than %1 characters.").arg(MaxCaption);
        return std::nullopt;
    }

    if (block.type == Type::Stream) {
        block.url = QUrl(json.value(QLatin1String("url")).toString());
        static const QStringList schemes{QStringLiteral("http"),
                                         QStringLiteral("https"),
                                         QStringLiteral("rtsp"),
                                         QStringLiteral("rtmp"),
                                         QStringLiteral("srt"),
                                         QStringLiteral("udp")};
        if (!block.url.isValid() || !schemes.contains(block.url.scheme())) {
            *error = QStringLiteral("A stream needs \"url\" with one of the schemes http, https, rtsp, rtmp, srt, udp (local files: use video or audio).");
            return std::nullopt;
        }
        return block;
    }

    QStringList given;
    if (json.contains(QLatin1String("path"))) {
        given.append(json.value(QLatin1String("path")).toString());
    }
    for (const QJsonValue &value : json.value(QLatin1String("paths")).toArray()) {
        given.append(value.toString());
    }
    // compare takes two plain strings, the easiest form for small local models; "paths" with two
    // entries is still accepted.
    if (block.type == Type::Compare) {
        for (const QLatin1String key : {QLatin1String("before"), QLatin1String("after")}) {
            if (json.contains(key)) {
                given.append(json.value(key).toString());
            }
        }
    }
    for (const QString &text : std::as_const(given)) {
        const QString path = localPath(text);
        if (path.isEmpty()) {
            *error = QStringLiteral("\"%1\" is not an absolute local path.").arg(text);
            return std::nullopt;
        }
        const QFileInfo info(path);
        if (!info.exists()) {
            *error = QStringLiteral("\"%1\" does not exist.").arg(path);
            return std::nullopt;
        }
        const bool wantsFolder = block.type == Type::Watch;
        const bool mayBeFolder = block.type == Type::Files || wantsFolder;
        if (info.isDir() && !mayBeFolder) {
            *error = QStringLiteral("\"%1\" is a folder; this block shows files.").arg(path);
            return std::nullopt;
        }
        if (wantsFolder && !info.isDir()) {
            *error = QStringLiteral("\"%1\" is not a folder; watch shows the newest image or video of a folder.").arg(path);
            return std::nullopt;
        }
        block.paths.append(path);
    }

    int wanted = 1;
    switch (block.type) {
    case Type::Files:
        wanted = -1;
        break;
    case Type::Compare:
        wanted = 2;
        break;
    default:
        break;
    }
    if (wanted == -1 && (block.paths.isEmpty() || block.paths.size() > MaxFiles)) {
        *error = QStringLiteral("A files block needs 1 to %1 entries in \"paths\".").arg(MaxFiles);
        return std::nullopt;
    }
    if (wanted > 0 && block.paths.size() != wanted) {
        *error = wanted == 1
            ? QStringLiteral("This block needs exactly one \"path\".")
            : QStringLiteral(
                  "compare needs \"before\" and \"after\", e.g. {\"type\": \"compare\", \"before\": \"/abs/path/a.png\", \"after\": \"/abs/path/b.png\"}.");
        return std::nullopt;
    }
    return block;
}

void WhaleAiCanvasModel::show(const QList<WhaleAiCanvasBlock> &blocks, bool replace)
{
    if (replace) {
        clear();
    }
    if (blocks.isEmpty()) {
        return;
    }
    const int start = int(m_entries.size());
    beginInsertRows(QModelIndex(), start, start + int(blocks.size()) - 1);
    for (const WhaleAiCanvasBlock &block : blocks) {
        auto entry = std::make_unique<Entry>();
        entry->block = block;
        if (block.type == WhaleAiCanvasBlock::Type::Watch) {
            startWatching(*entry);
        }
        m_entries.push_back(std::move(entry));
    }
    endInsertRows();
    const int excess = int(m_entries.size()) - MaxBlocks;
    if (excess > 0) {
        beginRemoveRows(QModelIndex(), 0, excess - 1);
        m_entries.erase(m_entries.begin(), m_entries.begin() + excess);
        endRemoveRows();
    }
    Q_EMIT countChanged();
}

void WhaleAiCanvasModel::clear()
{
    if (m_entries.empty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
    Q_EMIT countChanged();
}

const WhaleAiCanvasBlock &WhaleAiCanvasModel::block(int row) const
{
    return m_entries.at(row)->block;
}

QJsonArray WhaleAiCanvasModel::toJson() const
{
    QJsonArray blocks;
    for (const auto &entry : m_entries) {
        blocks.append(entry->block.toJson());
    }
    return blocks;
}

int WhaleAiCanvasModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant WhaleAiCanvasModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_entries.size())) {
        return QVariant();
    }
    const Entry &entry = *m_entries.at(index.row());
    const WhaleAiCanvasBlock &block = entry.block;
    switch (role) {
    case TypeRole:
        return WhaleAiCanvasBlock::typeName(block.type);
    case TitleRole:
        return block.title;
    case TextRole:
        return block.text;
    case UrlsRole: {
        if (block.type == WhaleAiCanvasBlock::Type::Stream) {
            return QStringList{block.url.toString()};
        }
        QStringList urls;
        for (const QString &path : block.paths) {
            urls.append(QUrl::fromLocalFile(path).toString());
        }
        return urls;
    }
    case NamesRole: {
        if (block.type == WhaleAiCanvasBlock::Type::Stream) {
            return QStringList{block.url.toDisplayString()};
        }
        QStringList names;
        for (const QString &path : block.paths) {
            names.append(QFileInfo(path).fileName());
        }
        return names;
    }
    case IconsRole: {
        const QMimeDatabase db;
        QStringList icons;
        for (const QString &path : block.paths) {
            icons.append(db.mimeTypeForFile(path).iconName());
        }
        return icons;
    }
    case CurrentRole:
        return entry.current.isEmpty() ? QString() : QUrl::fromLocalFile(entry.current).toString();
    case RevisionRole:
        return entry.revision;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> WhaleAiCanvasModel::roleNames() const
{
    return {
        {TypeRole, "blockType"},
        {TitleRole, "title"},
        {TextRole, "text"},
        {UrlsRole, "urls"},
        {NamesRole, "names"},
        {IconsRole, "icons"},
        {CurrentRole, "current"},
        {RevisionRole, "revision"},
    };
}

QString WhaleAiCanvasModel::newestMedia(const QString &folder)
{
    const QMimeDatabase db;
    QFileInfo newest;
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        if (!isMedia(db, info)) {
            continue;
        }
        if (!newest.exists() || info.lastModified() > newest.lastModified()
            || (info.lastModified() == newest.lastModified() && info.fileName() > newest.fileName())) {
            newest = info;
        }
    }
    return newest.exists() ? newest.absoluteFilePath() : QString();
}

void WhaleAiCanvasModel::startWatching(Entry &entry)
{
    const QString folder = entry.block.paths.constFirst();
    entry.current = newestMedia(folder);
    entry.watch = std::make_unique<KDirWatch>();
    // Files written in place (a render that overwrites its image) count too.
    entry.watch->addDir(folder, KDirWatch::WatchFiles);
    auto changed = [this, folder] {
        updateWatched(folder);
    };
    connect(entry.watch.get(), &KDirWatch::dirty, this, changed);
    connect(entry.watch.get(), &KDirWatch::created, this, changed);
    connect(entry.watch.get(), &KDirWatch::deleted, this, changed);
}

void WhaleAiCanvasModel::updateWatched(const QString &folder)
{
    for (int row = 0; row < int(m_entries.size()); ++row) {
        Entry &entry = *m_entries.at(row);
        if (entry.block.type != WhaleAiCanvasBlock::Type::Watch || entry.block.paths.constFirst() != folder) {
            continue;
        }
        entry.current = newestMedia(folder);
        ++entry.revision;
        Q_EMIT dataChanged(index(row), index(row), {CurrentRole, RevisionRole});
    }
}

#include "moc_whaleaicanvasmodel.cpp"
