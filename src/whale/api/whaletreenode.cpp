/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "whaletreenode.h"
#include "whaletreenode_p.h"

WhaleTreeNode::WhaleTreeNode() = default;
WhaleTreeNode::~WhaleTreeNode() = default;
WhaleTreeNode::WhaleTreeNode(const WhaleTreeNode &other) = default;
WhaleTreeNode &WhaleTreeNode::operator=(const WhaleTreeNode &other) = default;

WhaleTreeNode::WhaleTreeNode(const QExplicitlySharedDataPointer<const WhaleTreeNodeData> &data)
    : d(data)
{
}

bool WhaleTreeNode::isNull() const
{
    return !d;
}

QUrl WhaleTreeNode::url() const
{
    return d ? QUrl::fromLocalFile(d->path) : QUrl();
}

QString WhaleTreeNode::path() const
{
    return d ? d->path : QString();
}

QString WhaleTreeNode::name() const
{
    if (!d) {
        return {};
    }
    const qsizetype slash = d->path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 || d->path.size() == 1 ? d->path : d->path.mid(slash + 1);
}

WhaleTreeNode::Type WhaleTreeNode::type() const
{
    return d ? d->type : Type::Other;
}

bool WhaleTreeNode::isDirectory() const
{
    return d && d->type == Type::Directory;
}

int WhaleTreeNode::depth() const
{
    return d ? d->depth : 0;
}

qint64 WhaleTreeNode::size() const
{
    return d ? d->size : 0;
}

qint64 WhaleTreeNode::totalSize() const
{
    return d ? d->totalSize : 0;
}

int WhaleTreeNode::fileCount() const
{
    return d ? d->fileCount : 0;
}

int WhaleTreeNode::directoryCount() const
{
    return d ? d->directoryCount : 0;
}

bool WhaleTreeNode::isComplete() const
{
    return !d || d->complete;
}

QList<WhaleTreeNode> WhaleTreeNode::children() const
{
    return d ? d->children : QList<WhaleTreeNode>();
}
