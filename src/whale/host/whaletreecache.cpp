/*
 * SPDX-FileCopyrightText: 2026 Orcinus contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "whaletreecache.h"

#include "whaletreewatch.h"
#include "whaleviewdebug.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QPromise>
#include <QSet>
#include <QThreadPool>
#include <QtConcurrentRun>

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

int systemWatchLimit()
{
    QFile file(QStringLiteral("/proc/sys/fs/inotify/max_user_watches"));
    if (!file.open(QIODevice::ReadOnly)) {
        return 8192;
    }
    return file.readAll().trimmed().toInt();
}
}

WhaleTreeScanJob::WhaleTreeScanJob(const QUrl &root,
                                   const WhaleTreeScanner::Options &options,
                                   quint64 owner,
                                   const std::function<bool(const QByteArray &)> &watchFolder,
                                   QObject *parent)
    : QObject(parent)
    , m_watcher(new QFutureWatcher<WhaleTreeWalker::Outcome>(this))
    , m_owner(owner)
{
    connect(m_watcher, &QFutureWatcherBase::finished, this, &WhaleTreeScanJob::slotFinished);
    connect(m_watcher, &QFutureWatcherBase::progressValueChanged, this, [this](int visited) {
        if (visited > 0) { // the watcher also reports the start
            Q_EMIT progress(visited);
        }
    });
    m_watcher->setFuture(QtConcurrent::run(scanPool(), [root, options, watchFolder](QPromise<WhaleTreeWalker::Outcome> &promise) {
        WhaleTreeWalker::Callbacks callbacks;
        callbacks.canceled = [&promise]() {
            return promise.isCanceled();
        };
        callbacks.progress = [&promise](int visited) {
            promise.setProgressValue(visited);
        };
        callbacks.watchFolder = watchFolder;
        promise.addResult(WhaleTreeWalker::scan(root, options, callbacks));
    }));
}

WhaleTreeScanJob::~WhaleTreeScanJob()
{
    // The worker stops soon on its own; its outcome goes nowhere.
    m_watcher->disconnect(this);
    m_watcher->future().cancel();
}

quint64 WhaleTreeScanJob::owner() const
{
    return m_owner;
}

void WhaleTreeScanJob::addConsumer()
{
    ++m_consumers;
}

void WhaleTreeScanJob::removeConsumer()
{
    if (--m_consumers > 0 || !m_watcher->isRunning()) {
        return;
    }
    // Nobody waits any more.
    m_watcher->disconnect(this);
    m_watcher->future().cancel();
    Q_EMIT abandoned();
    deleteLater();
}

int WhaleTreeScanJob::consumers() const
{
    return m_consumers;
}

WhaleTreeWalker::Outcome WhaleTreeScanJob::outcome() const
{
    return m_outcome;
}

void WhaleTreeScanJob::markChanged()
{
    m_changed = true;
}

bool WhaleTreeScanJob::changedWhileScanning() const
{
    return m_changed;
}

void WhaleTreeScanJob::slotFinished()
{
    m_outcome = m_watcher->future().result();
    Q_EMIT done();
    deleteLater();
}

WhaleTreeCache::Limits WhaleTreeCache::configuredLimits()
{
    const KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("TreeScanCache"));
    Limits limits;
    limits.memoryBytes = qint64(std::max(0, group.readEntry("MemoryBudgetMiB", 128))) * 1024 * 1024;
    limits.watchedFolders = std::clamp(group.readEntry("WatchBudget", 10000), 0, systemWatchLimit() / 4);
    return limits;
}

WhaleTreeCache::WhaleTreeCache(const Limits &limits, QObject *parent)
    : QObject(parent)
    , m_limits(limits)
    , m_watch(new WhaleTreeWatch(limits.watchedFolders, this))
{
    connect(m_watch, &WhaleTreeWatch::changed, this, &WhaleTreeCache::slotChanged);
    connect(m_watch, &WhaleTreeWatch::overflowed, this, &WhaleTreeCache::slotOverflowed);
}

WhaleTreeCache::~WhaleTreeCache() = default;

size_t qHash(const WhaleTreeCache::Key &key, size_t seed)
{
    return qHashMulti(seed, key.root, key.maxDepth, key.stayOnFilesystem, key.includeHidden);
}

WhaleTreeCache::Key WhaleTreeCache::keyOf(const QUrl &root, const WhaleTreeScanner::Options &options)
{
    // Only options that change the tree; a negative depth always means "no limit".
    const QString path = root.isLocalFile() ? QDir::cleanPath(root.toLocalFile()) : root.toString();
    return Key{path, std::max(-1, options.maxDepth), options.stayOnFilesystem, options.includeHidden};
}

std::optional<WhaleTreeWalker::Outcome> WhaleTreeCache::lookup(const QUrl &root, const WhaleTreeScanner::Options &options)
{
    // Changes that happened before this request (e.g. by a file operation that has just
    // finished) must drop their trees first, even if their report is still queued.
    m_watch->processPendingEvents();
    const auto entry = m_entries.find(keyOf(root, options));
    if (entry == m_entries.end()) {
        return std::nullopt;
    }
    entry->lastUse = ++m_useCounter;
    ++m_statistics.hits;
    return entry->outcome;
}

WhaleTreeScanJob *WhaleTreeCache::job(const QUrl &root, const WhaleTreeScanner::Options &options)
{
    const Key key = keyOf(root, options);
    if (WhaleTreeScanJob *running = m_running.value(key)) {
        ++m_statistics.joins;
        return running;
    }
    const quint64 owner = m_nextOwner++;
    std::function<bool(const QByteArray &)> watchFolder;
    if (m_watch->isValid() && root.isLocalFile()) {
        watchFolder = m_watch->adder(owner);
    }
    auto job = new WhaleTreeScanJob(root, options, owner, watchFolder, this);
    m_running.insert(key, job);
    ++m_statistics.misses;
    // Connected before any consumer: the cache is up to date when they hear of it.
    connect(job, &WhaleTreeScanJob::done, this, [this, key, job]() {
        slotJobDone(key, job);
    });
    connect(job, &WhaleTreeScanJob::abandoned, this, [this, key, owner]() {
        m_running.remove(key);
        m_watch->release(owner);
    });
    return job;
}

void WhaleTreeCache::slotJobDone(const Key &key, WhaleTreeScanJob *job)
{
    m_running.remove(key);
    const WhaleTreeWalker::Outcome outcome = job->outcome();
    const quint64 owner = job->owner();
    const bool keep = !outcome.failed && !outcome.unwatched && !job->changedWhileScanning() && m_watch->isValid() && key.root.startsWith(QLatin1Char('/'))
        && outcome.estimatedBytes <= m_limits.memoryBytes;
    if (!keep) {
        ++m_statistics.notKept;
        m_watch->release(owner);
        return;
    }
    // A tree for the same key may be kept still (scanned again after it was dropped
    // and kept meanwhile by nobody else): the new one replaces it.
    dropEntry(key);
    m_entries.insert(key, Entry{outcome, owner, ++m_useCounter});
    m_entryOwners.insert(owner, key);
    m_bytes += outcome.estimatedBytes;
    evict(key);
}

void WhaleTreeCache::evict(const Key &keep)
{
    while (m_entries.count() > 1 && (m_bytes > m_limits.memoryBytes || m_watch->watchCount() > m_limits.watchedFolders)) {
        auto oldest = m_entries.end();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (!(it.key() == keep) && (oldest == m_entries.end() || it->lastUse < oldest->lastUse)) {
                oldest = it;
            }
        }
        if (oldest == m_entries.end()) {
            break;
        }
        dropEntry(oldest.key());
        ++m_statistics.evictions;
    }
}

void WhaleTreeCache::dropEntry(const Key &key)
{
    const auto entry = m_entries.constFind(key);
    if (entry == m_entries.constEnd()) {
        return;
    }
    m_watch->release(entry->owner);
    m_entryOwners.remove(entry->owner);
    m_bytes -= entry->outcome.estimatedBytes;
    m_entries.erase(entry);
}

void WhaleTreeCache::slotChanged(quint64 owner)
{
    if (const auto key = m_entryOwners.constFind(owner); key != m_entryOwners.constEnd()) {
        const QString root = key->root;
        dropEntry(*key);
        ++m_statistics.invalidations;
        Q_EMIT treeChanged(root);
        return;
    }
    for (auto it = m_running.cbegin(); it != m_running.cend(); ++it) {
        if (it.value() && it.value()->owner() == owner) {
            it.value()->markChanged();
            Q_EMIT treeChanged(it.key().root);
            return;
        }
    }
}

void WhaleTreeCache::slotOverflowed()
{
    qCWarning(WhaleViewDebug) << "Lost changes of watched folders; dropping all cached tree scans";
    m_statistics.invalidations += m_entries.count();
    QSet<QString> roots;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        roots.insert(it.key().root);
    }
    clear();
    for (auto it = m_running.cbegin(); it != m_running.cend(); ++it) {
        if (it.value()) {
            it.value()->markChanged();
            roots.insert(it.key().root);
        }
    }
    for (const QString &root : std::as_const(roots)) {
        Q_EMIT treeChanged(root);
    }
}

void WhaleTreeCache::clear()
{
    const QList<Key> keys = m_entries.keys();
    for (const Key &key : keys) {
        dropEntry(key);
    }
}

WhaleTreeCache::Statistics WhaleTreeCache::statistics() const
{
    Statistics statistics = m_statistics;
    statistics.entries = m_entries.count();
    statistics.bytes = m_bytes;
    statistics.watchedFolders = m_watch->watchCount();
    statistics.runningScans = m_running.count();
    return statistics;
}

WhaleTreeCache::Limits WhaleTreeCache::limits() const
{
    return m_limits;
}

#include "moc_whaletreecache.cpp"
