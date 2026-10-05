/*
 * SPDX-FileCopyrightText: 2026 Whale contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WHALETREESCANNERIMPL_H
#define WHALETREESCANNERIMPL_H

#include "whaletreescanner.h"

#include <functional>

template<typename T>
class QFutureWatcher;

namespace WhaleTreeScan
{
/// What a worker reports: the tree, or why there is none.
struct Outcome {
    WhaleTreeNode root;
    bool failed = false;
    WhaleTreeScanner::Error error = WhaleTreeScanner::Error::InvalidUrl;
    QString path;
};
}

/**
 * @brief The WhaleTreeScanner of a view: fts(3) in a worker thread.
 *
 * Each scan runs as one task of a thread pool of the scanners (QtConcurrent with a
 * QPromise). The worker walks the tree with fts_read(), so there is no recursion of
 * its own, and builds the nodes bottom-up with an explicit stack of open folders.
 * cancel() cancels the task's future; the worker checks it after every entry and
 * stops, and its outcome is never delivered. The scanner never waits for a worker.
 */
class WhaleTreeScannerImpl : public WhaleTreeScanner
{
    Q_OBJECT

public:
    explicit WhaleTreeScannerImpl(QObject *parent = nullptr);
    ~WhaleTreeScannerImpl() override;

    bool scan(const QUrl &root, const Options &options = Options()) override;
    void cancel() override;
    bool isRunning() const override;

    /**
     * @return The number of workers still running in this process, also those of
     *         canceled scans; for tests.
     */
    static int runningWorkers();

    /**
     * Tests only: calls @p hook in the worker thread with the path of every entry the
     * worker visits, before it is handled (e.g. to change files during a scan).
     */
    static void setVisitHookForTesting(const std::function<void(const QString &path)> &hook);

private:
    void slotWorkerFinished();

    QFutureWatcher<WhaleTreeScan::Outcome> *m_watcher = nullptr;
};

#endif
