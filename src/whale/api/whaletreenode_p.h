/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef WHALETREENODE_P_H
#define WHALETREENODE_P_H

#include "whaletreenode.h"

#include <QSharedData>

/**
 * The data of a WhaleTreeNode. Not part of the plugin API: only Whale's scanner fills
 * it, before the node is handed out; afterwards it is never changed.
 */
class WhaleTreeNodeData : public QSharedData
{
public:
    QString path;
    WhaleTreeNode::Type type = WhaleTreeNode::Type::File;
    int depth = 0;
    qint64 size = 0;
    qint64 totalSize = 0;
    int fileCount = 0;
    int directoryCount = 0;
    bool complete = true;
    QList<WhaleTreeNode> children;
};

#endif
