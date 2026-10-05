/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletreewalker.h"

#include "whaletreenode_p.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <vector>

#include <fts.h>
#include <sys/stat.h>

namespace
{
std::atomic<int> scans = 0;

std::function<void(const QString &)> &visitHook()
{
    static std::function<void(const QString &)> hook;
    return hook;
}

using Data = QExplicitlySharedDataPointer<WhaleTreeNodeData>;
using Outcome = WhaleTreeWalker::Outcome;

/// A folder whose entries are being visited.
struct OpenFolder {
    int level = 0;
    QByteArray path; // shared by the nodes of its entries
    Data data; // null below the depth limit
    qint64 totalSize = 0;
    int fileCount = 0;
    int directoryCount = 0;
    bool complete = true;
};

int compareNames(const FTSENT **a, const FTSENT **b)
{
    return std::strcmp((*a)->fts_name, (*b)->fts_name);
}

/// Files without blocks (pseudo files such as /proc/kcore) have no content to count.
qint64 contentSize(const FTSENT *entry)
{
    return entry->fts_statp->st_blocks > 0 ? qint64(entry->fts_statp->st_size) : 0;
}

Outcome failure(WhaleTreeScanner::Error error, const QString &path)
{
    Outcome outcome;
    outcome.failed = true;
    outcome.error = error;
    outcome.path = path;
    return outcome;
}

WhaleTreeScanner::Error rootError(int error)
{
    if (error == ENOENT || error == ENOTDIR) {
        return WhaleTreeScanner::Error::NotFound;
    }
    return WhaleTreeScanner::Error::AccessDenied;
}

class Walker
{
public:
    Walker(const WhaleTreeScanner::Options &options, const WhaleTreeWalker::Callbacks &callbacks)
        : m_options(options)
        , m_callbacks(callbacks)
    {
    }

    Outcome walk(const QString &rootPath)
    {
        QByteArray encoded = QFile::encodeName(rootPath);
        char *paths[] = {encoded.data(), nullptr};
        // Physical: links are entries and never followed, except a link given as root.
        int flags = FTS_PHYSICAL | FTS_COMFOLLOW | FTS_NOCHDIR;
        if (m_options.stayOnFilesystem) {
            flags |= FTS_XDEV;
        }
        FTS *tree = ::fts_open(paths, flags, compareNames);
        if (!tree) {
            return failure(rootError(errno), rootPath);
        }

        Outcome outcome;
        QElapsedTimer progressTimer;
        progressTimer.start();
        int visited = 0;
        FTSENT *entry;
        while (!m_callbacks.canceled() && (entry = ::fts_read(tree))) {
            if (const auto &hook = visitHook()) {
                hook(QFile::decodeName(entry->fts_path));
            }
            if (!visit(tree, entry, &outcome)) {
                break;
            }
            ++visited;
            if (progressTimer.hasExpired(100)) {
                m_callbacks.progress(visited);
                progressTimer.restart();
            }
        }
        ::fts_close(tree);
        outcome.estimatedBytes = m_bytes;
        outcome.nodeCount = m_nodes;
        outcome.unwatched = m_unwatched;
        return outcome;
    }

private:
    bool excluded(const FTSENT *entry) const
    {
        return !m_options.includeHidden && entry->fts_level > 0 && entry->fts_name[0] == '.';
    }

    /// @return False if the scan cannot go on (the root failed).
    bool visit(FTS *tree, FTSENT *entry, Outcome *outcome)
    {
        const int level = entry->fts_level;

        switch (entry->fts_info) {
        case FTS_D:
            if (excluded(entry)) {
                // Not entered; fts reports it once more as FTS_DP, which is skipped too.
                ::fts_set(tree, entry, FTS_SKIP);
                entry->fts_number = SkippedFolder;
                return true;
            }
            openFolder(tree, entry);
            return true;
        case FTS_DP:
            if (entry->fts_number != SkippedFolder) {
                closeFolder(outcome);
            }
            return true;
        case FTS_DNR:
        case FTS_ERR:
            if (level == 0) {
                *outcome = failure(rootError(entry->fts_errno), QFile::decodeName(entry->fts_path));
                return false;
            }
            if (!m_open.empty() && m_open.back().level == level) {
                // The folder that was just opened could not be read (no permission, gone).
                m_open.back().complete = false;
                closeFolder(outcome);
            } else if (!excluded(entry)) {
                // Reported without being opened: an entry that could not be read.
                addLeaf(entry, WhaleTreeNode::Type::Directory, 0, false);
            }
            return true;
        case FTS_NS:
            if (level == 0) {
                *outcome = failure(rootError(entry->fts_errno), QFile::decodeName(entry->fts_path));
                return false;
            }
            return true; // vanished since its folder was read: left out
        case FTS_DC:
            // A folder that is its own ancestor (only through bind mounts, as links are
            // not followed): an entry, not entered.
            if (!excluded(entry)) {
                addLeaf(entry, WhaleTreeNode::Type::Directory, 0, false);
            }
            return true;
        default:
            break;
        }

        if (level == 0) {
            *outcome = failure(WhaleTreeScanner::Error::NotAFolder, QFile::decodeName(entry->fts_path));
            return false;
        }
        if (excluded(entry)) {
            return true;
        }
        switch (entry->fts_info) {
        case FTS_F:
            addLeaf(entry, WhaleTreeNode::Type::File, contentSize(entry), true);
            break;
        case FTS_SL:
        case FTS_SLNONE:
            addLeaf(entry, WhaleTreeNode::Type::Symlink, 0, true);
            break;
        default:
            addLeaf(entry, WhaleTreeNode::Type::Other, contentSize(entry), true);
            break;
        }
        return true;
    }

    bool hasNode(int level) const
    {
        return m_options.maxDepth < 0 || level <= m_options.maxDepth;
    }

    Data newData(const FTSENT *entry, WhaleTreeNode::Type type)
    {
        Data data(new WhaleTreeNodeData);
        if (entry->fts_level == 0) {
            data->name = QByteArray(entry->fts_path);
        } else {
            data->parentPath = m_open.back().path;
            data->name = QByteArray(entry->fts_name, entry->fts_namelen);
        }
        data->type = type;
        data->depth = entry->fts_level;
        ++m_nodes;
        // The node, its name (with the array header), its slot in its folder's list
        // and the allocator's overhead; the shared folder path is counted per folder.
        m_bytes += qint64(sizeof(WhaleTreeNodeData)) + 16 + data->name.size() + 24 + 8 + 16;
        return data;
    }

    void openFolder(FTS *tree, FTSENT *entry)
    {
        OpenFolder folder;
        folder.level = entry->fts_level;
        if (folder.level == 0) {
            m_rootDevice = entry->fts_statp->st_dev;
        } else if (m_options.stayOnFilesystem && entry->fts_statp->st_dev != m_rootDevice) {
            // A mount point: listed, not entered (FTS_XDEV does the same).
            ::fts_set(tree, entry, FTS_SKIP);
            folder.complete = false;
        }
        if (hasNode(folder.level)) {
            folder.data = newData(entry, WhaleTreeNode::Type::Directory);
        }
        folder.path = QByteArray(entry->fts_path, entry->fts_pathlen);
        m_bytes += folder.path.size() + 24;
        // Watched before its entries are read, so no change of them can be missed.
        if (folder.complete && !m_unwatched && m_callbacks.watchFolder && !m_callbacks.watchFolder(folder.path)) {
            m_unwatched = true;
        }
        m_open.push_back(std::move(folder));
    }

    void closeFolder(Outcome *outcome)
    {
        OpenFolder folder = std::move(m_open.back());
        m_open.pop_back();
        if (folder.data) {
            folder.data->totalSize = folder.totalSize;
            folder.data->fileCount = folder.fileCount;
            folder.data->directoryCount = folder.directoryCount;
            folder.data->complete = folder.complete;
            folder.data->children.squeeze();
        }
        if (m_open.empty()) {
            outcome->root = WhaleTreeNode(folder.data);
            return;
        }
        OpenFolder &parent = m_open.back();
        parent.totalSize += folder.totalSize;
        parent.fileCount += folder.fileCount;
        parent.directoryCount += folder.directoryCount + 1;
        parent.complete = parent.complete && folder.complete;
        if (folder.data && parent.data) {
            parent.data->children.append(WhaleTreeNode(folder.data));
        }
    }

    void addLeaf(const FTSENT *entry, WhaleTreeNode::Type type, qint64 size, bool complete)
    {
        if (m_open.empty()) {
            return;
        }
        OpenFolder &parent = m_open.back();
        parent.totalSize += size;
        if (type == WhaleTreeNode::Type::Directory) {
            ++parent.directoryCount;
        } else {
            ++parent.fileCount;
        }
        parent.complete = parent.complete && complete;
        if (parent.data && hasNode(entry->fts_level)) {
            Data data = newData(entry, type);
            data->size = size;
            data->totalSize = size;
            data->complete = complete;
            parent.data->children.append(WhaleTreeNode(data));
        }
    }

    // fts_number of a hidden folder that is not entered.
    static constexpr long SkippedFolder = 1;

    const WhaleTreeScanner::Options m_options;
    const WhaleTreeWalker::Callbacks &m_callbacks;
    std::vector<OpenFolder> m_open;
    dev_t m_rootDevice = 0;
    qint64 m_bytes = 0;
    int m_nodes = 0;
    bool m_unwatched = false;
};
}

WhaleTreeWalker::Outcome WhaleTreeWalker::scan(const QUrl &root, const WhaleTreeScanner::Options &options, const Callbacks &callbacks)
{
    ++scans;
    Outcome outcome;
    if (root.isEmpty() || !root.isValid()) {
        outcome = failure(WhaleTreeScanner::Error::InvalidUrl, root.toDisplayString());
    } else if (!root.isLocalFile()) {
        outcome = failure(WhaleTreeScanner::Error::NotLocal, root.toDisplayString());
    } else {
        outcome = Walker(options, callbacks).walk(QDir::cleanPath(root.toLocalFile()));
    }
    --scans;
    return outcome;
}

int WhaleTreeWalker::runningScans()
{
    return scans;
}

void WhaleTreeWalker::setVisitHookForTesting(const std::function<void(const QString &)> &hook)
{
    visitHook() = hook;
}
