/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETREECACHE_H
#define WHALETREECACHE_H

#include "whaletreewalker.h"

#include <QHash>
#include <QObject>
#include <QPointer>

#include <functional>
#include <optional>

template<typename T>
class QFutureWatcher;
class WhaleTreeWatch;

/**
 * @brief One running scan (fts in a worker thread), shared by the scanners that wait
 * for it.
 *
 * Scanners join as consumers. A consumer that leaves (cancel) does not end the scan for
 * the others; when the last one leaves before the scan is done, the worker is canceled
 * and abandoned() follows. After done() or abandoned() the job deletes itself.
 */
class WhaleTreeScanJob : public QObject
{
    Q_OBJECT

public:
    /**
     * @param watchFolder Watches the folders the scan enters (see WhaleTreeWatch); may be empty.
     */
    WhaleTreeScanJob(const QUrl &root,
                     const WhaleTreeScanner::Options &options,
                     quint64 owner,
                     const std::function<bool(const QByteArray &)> &watchFolder,
                     QObject *parent = nullptr);
    ~WhaleTreeScanJob() override;

    quint64 owner() const;
    void addConsumer();
    void removeConsumer();
    int consumers() const;

    /// The outcome; valid once done() was emitted.
    WhaleTreeWalker::Outcome outcome() const;

    /// A watched folder changed while the scan ran: its tree may be outdated already.
    void markChanged();
    bool changedWhileScanning() const;

Q_SIGNALS:
    void progress(int visitedEntries);
    void done();
    void abandoned();

private:
    void slotFinished();

    QFutureWatcher<WhaleTreeWalker::Outcome> *m_watcher;
    const quint64 m_owner;
    int m_consumers = 0;
    bool m_changed = false;
    WhaleTreeWalker::Outcome m_outcome;
};

/**
 * @brief The tree scans of a main window, shared by all its views (WhaleTreeScanner).
 *
 * Owned by WhaleViewActions (one per main window, like the view registry); the scanners
 * of the views' contexts use it. Scanners without a cache scan on their own.
 *
 * - Key: the cleaned local path and every option that changes the result (maxDepth,
 *   stayOnFilesystem, includeHidden).
 * - A finished, unchanged tree is kept and handed out again as long as its folders are
 *   watched and nothing changed in them (WhaleTreeWatch, one watch per folder). Any
 *   change below the root drops the whole tree (no repair of parts).
 * - Scans for the same key run once at a time; later requests join the running one.
 * - Budgets: the estimated memory of the kept trees, and the number of watched folders.
 *   When a tree is added, the least recently used trees go until both fit; a tree that
 *   alone does not fit is not kept. Trees handed out live on as long as someone holds
 *   them; the budget only covers what the cache keeps.
 * - Not kept: failed scans, trees that changed while scanning, trees whose folders
 *   could not all be watched.
 */
class WhaleTreeCache : public QObject
{
    Q_OBJECT

public:
    struct Limits {
        qint64 memoryBytes = 128 * 1024 * 1024;
        int watchedFolders = 10000;
    };

    /**
     * @return The limits from Whale's configuration (group "TreeScanCache", keys
     *         "MemoryBudgetMiB" and "WatchBudget"); the watch budget never exceeds a
     *         quarter of the system's inotify limit.
     */
    static Limits configuredLimits();

    explicit WhaleTreeCache(const Limits &limits, QObject *parent = nullptr);
    ~WhaleTreeCache() override;

    /**
     * @return The kept tree for @p root and @p options, if there is one; it counts as
     *         used now.
     */
    std::optional<WhaleTreeWalker::Outcome> lookup(const QUrl &root, const WhaleTreeScanner::Options &options);

    /**
     * @return The running scan for @p root and @p options, started now if there is none.
     *         The caller joins it as a consumer.
     */
    WhaleTreeScanJob *job(const QUrl &root, const WhaleTreeScanner::Options &options);

    /// Drops all kept trees.
    void clear();

    struct Statistics {
        int hits = 0;
        int misses = 0; ///< scans started
        int joins = 0; ///< requests that joined a running scan
        int invalidations = 0; ///< kept trees dropped because something changed
        int evictions = 0; ///< kept trees dropped for the budgets
        int notKept = 0; ///< finished scans that were not kept
        int entries = 0;
        qint64 bytes = 0;
        int watchedFolders = 0;
        int runningScans = 0;
    };
    Statistics statistics() const;
    Limits limits() const;

private:
    struct Key {
        QString root;
        int maxDepth;
        bool stayOnFilesystem;
        bool includeHidden;
        bool operator==(const Key &other) const = default;
    };
    friend size_t qHash(const Key &key, size_t seed);

    struct Entry {
        WhaleTreeWalker::Outcome outcome;
        quint64 owner;
        quint64 lastUse;
    };

    static Key keyOf(const QUrl &root, const WhaleTreeScanner::Options &options);
    void slotJobDone(const Key &key, WhaleTreeScanJob *job);
    void slotChanged(quint64 owner);
    void slotOverflowed();
    void dropEntry(const Key &key);
    void evict(const Key &keep);

    const Limits m_limits;
    WhaleTreeWatch *m_watch;
    QHash<Key, Entry> m_entries;
    QHash<quint64, Key> m_entryOwners;
    QHash<Key, QPointer<WhaleTreeScanJob>> m_running;
    quint64 m_nextOwner = 1;
    quint64 m_useCounter = 0;
    qint64 m_bytes = 0;
    Statistics m_statistics;
};

#endif
