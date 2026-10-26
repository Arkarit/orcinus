/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whalerecursiveprojection.h"

#include <KIO/UDSEntry>

#include <QFile>
#include <QUrl>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
struct FileStat {
    mode_t mode = 0;
    qint64 size = 0;
    uid_t uid = 0;
    gid_t gid = 0;
    qint64 modified = 0;
    qint64 accessed = 0;
    qint64 created = -1; // -1: unknown
};

bool fileStat(const QByteArray &path, bool followLink, FileStat *result)
{
#ifdef Q_OS_LINUX
    // statx() for the birth time, as KIO's file worker does.
    struct statx buffer;
    if (statx(AT_FDCWD, path.constData(), followLink ? 0 : AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_BTIME, &buffer) != 0) {
        return false;
    }
    result->mode = buffer.stx_mode;
    result->size = qint64(buffer.stx_size);
    result->uid = buffer.stx_uid;
    result->gid = buffer.stx_gid;
    result->modified = buffer.stx_mtime.tv_sec;
    result->accessed = buffer.stx_atime.tv_sec;
    result->created = (buffer.stx_mask & STATX_BTIME) ? qint64(buffer.stx_btime.tv_sec) : -1;
#else
    QT_STATBUF buffer;
    if ((followLink ? QT_STAT(path.constData(), &buffer) : QT_LSTAT(path.constData(), &buffer)) != 0) {
        return false;
    }
    result->mode = buffer.st_mode;
    result->size = qint64(buffer.st_size);
    result->uid = buffer.st_uid;
    result->gid = buffer.st_gid;
    result->modified = buffer.st_mtime;
    result->accessed = buffer.st_atime;
#endif
    return true;
}

// The entry KIO's file worker lists for @p path: for a symbolic link the link target and
// the target's details, for a broken one KIO's "unknown type, all permissions".
KFileItem itemOf(const QByteArray &path, bool *isFolder)
{
    *isFolder = false;
    FileStat stat;
    if (!fileStat(path, false, &stat)) {
        return KFileItem();
    }

    QByteArray linkTarget;
    mode_t type = stat.mode & S_IFMT;
    mode_t access = stat.mode & 07777;
    if (S_ISLNK(stat.mode)) {
        linkTarget.resize(4096);
        const ssize_t length = readlink(path.constData(), linkTarget.data(), linkTarget.size());
        linkTarget.resize(length > 0 ? length : 0);
        FileStat target;
        if (fileStat(path, true, &target)) {
            stat = target;
            type = stat.mode & S_IFMT;
            access = stat.mode & 07777;
        } else {
            type = S_IFMT - 1;
            access = S_IRWXU | S_IRWXG | S_IRWXO;
            stat.size = 0;
        }
    }
    if (type == S_IFDIR) {
        *isFolder = true;
        return KFileItem();
    }

    const QString localPath = QFile::decodeName(path);
    const int slash = path.lastIndexOf('/');
    KIO::UDSEntry entry;
    entry.reserve(10);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, QFile::decodeName(path.mid(slash + 1)));
    if (!linkTarget.isEmpty()) {
        entry.fastInsert(KIO::UDSEntry::UDS_LINK_DEST, QFile::decodeName(linkTarget));
    }
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, type);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, access);
    entry.fastInsert(KIO::UDSEntry::UDS_SIZE, stat.size);
    entry.fastInsert(KIO::UDSEntry::UDS_LOCAL_USER_ID, stat.uid);
    entry.fastInsert(KIO::UDSEntry::UDS_LOCAL_GROUP_ID, stat.gid);
    entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, stat.modified);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS_TIME, stat.accessed);
    if (stat.created >= 0) {
        entry.fastInsert(KIO::UDSEntry::UDS_CREATION_TIME, stat.created);
    }
    // Delayed MIME types, as Dolphin's directory lister has them: KFileItemModelRolesUpdater
    // determines them for the visible items.
    return KFileItem(entry, QUrl::fromLocalFile(localPath), true, false);
}
}

WhaleRecursiveProjection::Result WhaleRecursiveProjection::project(const WhaleTreeNode &root, const std::function<bool()> &canceled)
{
    Result result;
    result.items.reserve(root.fileCount());

    QList<WhaleTreeNode> folders{root};
    int visited = 0;
    while (!folders.isEmpty()) {
        const WhaleTreeNode folder = folders.takeLast();
        const QList<WhaleTreeNode> children = folder.children();
        for (const WhaleTreeNode &child : children) {
            if (canceled && (++visited % 256) == 0 && canceled()) {
                result.canceled = true;
                return result;
            }
            if (child.type() == WhaleTreeNode::Type::Directory) {
                folders.append(child);
                continue;
            }
            bool isFolder = false;
            const KFileItem item = itemOf(QFile::encodeName(child.path()), &isFolder);
            if (isFolder) {
                ++result.folderLinks;
            } else if (item.isNull()) {
                ++result.vanished;
            } else {
                result.items.append(item);
            }
        }
    }
    return result;
}

KFileItem WhaleRecursiveProjection::fileItem(const QString &path, bool *isFolder)
{
    bool folder = false;
    const KFileItem item = itemOf(QFile::encodeName(path), &folder);
    if (isFolder) {
        *isFolder = folder;
    }
    return item;
}
