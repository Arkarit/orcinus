/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALETREENODE_H
#define WHALETREENODE_H

#include "whaleviewapi_export.h"

#include <QExplicitlySharedDataPointer>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QUrl>

class WhaleTreeNodeData;

/**
 * @brief One entry of a scanned folder tree (WhaleTreeScanner), with its sizes.
 *
 * A node is an immutable snapshot: once a scan has delivered it, it never changes,
 * and it can be read in any thread. Copies are cheap; a node and its children stay
 * valid as long as any copy of them exists.
 *
 * Sizes are apparent sizes in bytes (as "ls -l" shows them), taken when the scan
 * visited the entry:
 * - size(): the entry's own size. The content size of a file; 0 for folders and
 *   symbolic links (a link's target is never counted) and for files that occupy no
 *   blocks (such as pseudo files in /proc).
 * - totalSize(): size() plus the totalSize() of everything below, including what lies
 *   beyond the scan's depth limit.
 */
class WHALEVIEWAPI_EXPORT WhaleTreeNode
{
public:
    enum class Type {
        File,
        Directory,
        /// A symbolic link, also a broken one. Links are never followed.
        Symlink,
        /// A device, pipe, socket or another special file.
        Other,
    };

    /// A null node.
    WhaleTreeNode();
    ~WhaleTreeNode();
    WhaleTreeNode(const WhaleTreeNode &other);
    WhaleTreeNode &operator=(const WhaleTreeNode &other);

    bool isNull() const;

    QUrl url() const;
    /// The local path, without a trailing slash (except for "/").
    QString path() const;
    /// The last part of path().
    QString name() const;
    Type type() const;
    bool isDirectory() const;

    /// 0 for the scanned root, 1 for its entries, and so on.
    int depth() const;

    /// The entry's own size, see the class description.
    qint64 size() const;

    /// The size of the entry and everything below it, see the class description.
    qint64 totalSize() const;

    /// Number of non-folder entries below (files, links, special files), at any depth.
    int fileCount() const;

    /// Number of folders below, at any depth, not counting this one.
    int directoryCount() const;

    /**
     * @return False if something below could not be included in totalSize(): a folder
     *         that could not be read (no permission, vanished during the scan), or a
     *         folder on another file system that the scan did not enter.
     */
    bool isComplete() const;

    /**
     * @return The entries of a folder, sorted by name (byte order). Empty below the
     *         scan's depth limit, even if the folder has content (see totalSize()).
     */
    QList<WhaleTreeNode> children() const;

    /// For Whale's scanner: a node from its data (whaletreenode_p.h).
    explicit WhaleTreeNode(const QExplicitlySharedDataPointer<const WhaleTreeNodeData> &data);

private:
    QExplicitlySharedDataPointer<const WhaleTreeNodeData> d;
};

Q_DECLARE_METATYPE(WhaleTreeNode)

#endif
