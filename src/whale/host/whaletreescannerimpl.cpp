/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletreescannerimpl.h"

#include "whaletreenode_p.h"

#include <KLocalizedString>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <vector>

#include <fts.h>
#include <sys/stat.h>

namespace
{
// Scans run in a pool of their own, so long scans never hold up other users of
// Qt's global pool.
QThreadPool *scanPool()
{
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(4);
        return true;
    }();
    Q_UNUSED(configured)
    return &pool;
}

std::atomic<int> workers = 0;

std::function<void(const QString &)> &visitHook()
{
    static std::function<void(const QString &)> hook;
    return hook;
}

using Data = QExplicitlySharedDataPointer<WhaleTreeNodeData>;

/// A folder whose entries are being visited.
struct OpenFolder {
    int level = 0;
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

Data newData(const FTSENT *entry, WhaleTreeNode::Type type)
{
    Data data(new WhaleTreeNodeData);
    data->path = QFile::decodeName(entry->fts_path);
    data->type = type;
    data->depth = entry->fts_level;
    return data;
}

/// Files without blocks (pseudo files such as /proc/kcore) have no content to count.
qint64 contentSize(const FTSENT *entry)
{
    return entry->fts_statp->st_blocks > 0 ? qint64(entry->fts_statp->st_size) : 0;
}

WhaleTreeScan::Outcome failure(WhaleTreeScanner::Error error, const QString &path)
{
    WhaleTreeScan::Outcome outcome;
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
    Walker(QPromise<WhaleTreeScan::Outcome> &promise, const WhaleTreeScanner::Options &options)
        : m_promise(promise)
        , m_options(options)
    {
    }

    WhaleTreeScan::Outcome walk(const QString &rootPath)
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

        WhaleTreeScan::Outcome outcome;
        QElapsedTimer progressTimer;
        progressTimer.start();
        int visited = 0;
        FTSENT *entry;
        while (!m_promise.isCanceled() && (entry = ::fts_read(tree))) {
            if (const auto &hook = visitHook()) {
                hook(QFile::decodeName(entry->fts_path));
            }
            if (!visit(tree, entry, &outcome)) {
                break;
            }
            ++visited;
            if (progressTimer.hasExpired(100)) {
                m_promise.setProgressValue(visited);
                progressTimer.restart();
            }
        }
        ::fts_close(tree);
        return outcome;
    }

private:
    /// @return False if the scan cannot go on (the root failed).
    bool visit(FTS *tree, FTSENT *entry, WhaleTreeScan::Outcome *outcome)
    {
        const int level = entry->fts_level;
        const bool hidden = level > 0 && entry->fts_name[0] == '.';

        switch (entry->fts_info) {
        case FTS_D:
            if (hidden && !m_options.includeHidden) {
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
            } else if (!(hidden && !m_options.includeHidden)) {
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
            if (!(hidden && !m_options.includeHidden)) {
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
        if (hidden && !m_options.includeHidden) {
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
        m_open.push_back(std::move(folder));
    }

    void closeFolder(WhaleTreeScan::Outcome *outcome)
    {
        OpenFolder folder = std::move(m_open.back());
        m_open.pop_back();
        if (folder.data) {
            folder.data->totalSize = folder.totalSize;
            folder.data->fileCount = folder.fileCount;
            folder.data->directoryCount = folder.directoryCount;
            folder.data->complete = folder.complete;
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

    QPromise<WhaleTreeScan::Outcome> &m_promise;
    const WhaleTreeScanner::Options m_options;
    std::vector<OpenFolder> m_open;
    dev_t m_rootDevice = 0;
};

void runScan(QPromise<WhaleTreeScan::Outcome> &promise, const QUrl &root, const WhaleTreeScanner::Options &options)
{
    ++workers;
    WhaleTreeScan::Outcome outcome;
    if (root.isEmpty() || !root.isValid()) {
        outcome = failure(WhaleTreeScanner::Error::InvalidUrl, root.toDisplayString());
    } else if (!root.isLocalFile()) {
        outcome = failure(WhaleTreeScanner::Error::NotLocal, root.toDisplayString());
    } else {
        outcome = Walker(promise, options).walk(QDir::cleanPath(root.toLocalFile()));
    }
    promise.addResult(outcome);
    --workers;
}
}

WhaleTreeScannerImpl::WhaleTreeScannerImpl(QObject *parent)
    : WhaleTreeScanner(parent)
{
}

WhaleTreeScannerImpl::~WhaleTreeScannerImpl()
{
    // Quietly: the worker stops on its own soon, and its outcome goes nowhere.
    if (m_watcher) {
        m_watcher->disconnect(this);
        m_watcher->future().cancel();
    }
}

bool WhaleTreeScannerImpl::scan(const QUrl &root, const Options &options)
{
    if (m_watcher) {
        return false;
    }
    m_watcher = new QFutureWatcher<WhaleTreeScan::Outcome>(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, &WhaleTreeScannerImpl::slotWorkerFinished);
    connect(m_watcher, &QFutureWatcherBase::progressValueChanged, this, [this](int visited) {
        if (visited > 0) { // the watcher also reports the start
            Q_EMIT progress(visited);
        }
    });
    Q_EMIT started(root);
    m_watcher->setFuture(QtConcurrent::run(scanPool(), runScan, root, options));
    return true;
}

void WhaleTreeScannerImpl::cancel()
{
    if (!m_watcher) {
        return;
    }
    m_watcher->disconnect(this);
    m_watcher->future().cancel();
    m_watcher->deleteLater();
    m_watcher = nullptr;
    Q_EMIT canceled();
}

bool WhaleTreeScannerImpl::isRunning() const
{
    return m_watcher != nullptr;
}

int WhaleTreeScannerImpl::runningWorkers()
{
    return workers;
}

void WhaleTreeScannerImpl::setVisitHookForTesting(const std::function<void(const QString &)> &hook)
{
    visitHook() = hook;
}

void WhaleTreeScannerImpl::slotWorkerFinished()
{
    QFutureWatcher<WhaleTreeScan::Outcome> *watcher = m_watcher;
    m_watcher = nullptr;
    watcher->deleteLater();
    const WhaleTreeScan::Outcome outcome = watcher->future().result();
    if (!outcome.failed) {
        Q_EMIT finished(outcome.root);
        return;
    }

    QString message;
    switch (outcome.error) {
    case Error::InvalidUrl:
        message = i18nc("@info", "The location is not valid.");
        break;
    case Error::NotLocal:
        message = i18nc("@info %1 is a location", "Only local folders can be scanned, not %1.", outcome.path);
        break;
    case Error::NotFound:
        message = i18nc("@info %1 is a path", "The folder %1 does not exist.", outcome.path);
        break;
    case Error::NotAFolder:
        message = i18nc("@info %1 is a path", "%1 is not a folder.", outcome.path);
        break;
    case Error::AccessDenied:
        message = i18nc("@info %1 is a path", "The folder %1 cannot be read.", outcome.path);
        break;
    }
    Q_EMIT failed(outcome.error, message);
}

#include "moc_whaletreescannerimpl.cpp"
