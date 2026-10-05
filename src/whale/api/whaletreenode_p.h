/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALETREENODE_P_H
#define WHALETREENODE_P_H

#include "whaletreenode.h"

#include <QByteArray>
#include <QSharedData>

/**
 * The data of a WhaleTreeNode. Not part of the plugin API: only Whale's scanner fills
 * it, before the node is handed out; afterwards it is never changed.
 *
 * Compact on purpose: a node stores its name, and its folder's path as an implicitly
 * shared byte array, which all entries of a folder share. So a full path exists once
 * per folder, not once per entry, and no node refers to its parent. Names and paths
 * are in the local 8-bit encoding of the file system, as fts(3) returns them.
 */
class WhaleTreeNodeData : public QSharedData
{
public:
    /// The path of the folder that contains this entry; empty for the root.
    QByteArray parentPath;
    /// The entry's name; for the root its whole path.
    QByteArray name;
    int depth = 0;
    int fileCount = 0;
    int directoryCount = 0;
    qint64 size = 0;
    qint64 totalSize = 0;
    QList<WhaleTreeNode> children;
    WhaleTreeNode::Type type = WhaleTreeNode::Type::File;
    bool complete = true;

    /// @return The encoded full path.
    QByteArray encodedPath() const
    {
        if (parentPath.isEmpty()) {
            return name;
        }
        if (parentPath.endsWith('/')) {
            return QByteArray(parentPath + name);
        }
        return QByteArray(parentPath + '/' + name);
    }
};

#endif
