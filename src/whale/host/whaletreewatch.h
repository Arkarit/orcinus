/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETREEWATCH_H
#define WHALETREEWATCH_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>

class QSocketNotifier;

/**
 * @brief Watches the folders of cached tree scans for changes (inotify).
 *
 * Folders are watched for an owner (a scan, then its cache entry). Any change in a
 * watched folder, of its entries' names or of a file's content, reports changed() for
 * every owner of that folder; the cache then drops the owner's tree as a whole.
 *
 * Why not KDirWatch: a scan must watch a folder before it reads the folder's entries,
 * or a change between reading and watching would never be noticed. The scan runs in
 * a worker thread, and KDirWatch only works in the thread it belongs to. inotify
 * watches can be added from any thread; their events are read here, in the thread of
 * this object. Like KDirWatch's inotify backend, this costs one watch per folder.
 *
 * The functions of adder() may be called from any thread; everything else only from
 * the object's thread.
 */
class WhaleTreeWatch : public QObject
{
    Q_OBJECT

public:
    /**
     * @param budget The number of folders that may be watched at a time, for all owners.
     */
    explicit WhaleTreeWatch(int budget, QObject *parent = nullptr);
    ~WhaleTreeWatch() override;

    bool isValid() const;
    int budget() const;

    /**
     * @return A function that watches a folder for @p owner, for a worker thread. It
     *         stays safe to call after this object is gone (it then watches nothing).
     *         It returns false if the folder is not watched: the budget is used up (an
     *         owner may use at most the budget; all owners together at most twice the
     *         budget, while new scans run beside cached entries), or inotify failed.
     */
    std::function<bool(const QByteArray &path)> adder(quint64 owner) const;

    /**
     * Stops watching the folders of @p owner (those that no other owner watches). The
     * owner can watch nothing afterwards, also not from a worker that still runs.
     */
    void release(quint64 owner);

    /// The number of folders watched now.
    int watchCount() const;

    /// The number of folders watched for @p owner.
    int watchCount(quint64 owner) const;

Q_SIGNALS:
    /**
     * Something changed in a folder of @p owner.
     */
    void changed(quint64 owner);

    /**
     * Events were lost (the kernel's queue overflowed): any watched tree may be stale.
     */
    void overflowed();

private:
    void readEvents();

    /// The thread-safe part, shared with the workers.
    struct Core {
        ~Core();
        bool add(quint64 owner, const QByteArray &path);

        int fd = -1;
        int budget = 0;
        bool closed = false;
        QMutex mutex;
        QHash<int, QSet<quint64>> owners; // watch descriptor -> owners
        QHash<quint64, QList<int>> watches; // owner -> watch descriptors
        QSet<quint64> released;
    };

    const std::shared_ptr<Core> m_core;
    QSocketNotifier *m_notifier = nullptr;
};

#endif
